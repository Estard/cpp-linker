#include "cppld.hpp"
#include "statusreport.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>
using namespace std::literals;
namespace cppld {

namespace /*internal*/ {

struct Option {
    enum class Type {
        ignore = 0,
        setOutputFileName,
        setEntrySymbolName,
        searchForLibrary,
        addLibrarySearchPath,
        disableSharedLinking,
        enableSharedLinking,
        pushBState,
        popBState,
        enableEhFrameHdr,
        disableEhFrameHdr,
        buildID,
        keyword,
        unrecognized
    } type{Type::ignore};

    bool hasArg{false};
};

constexpr bool noArg = false;
constexpr bool hasArg = true;

// Can't switch in C++ on a string, need to create an enum for the switch as well as map from string to option
const std::unordered_map<char, Option> shortOptions{
    {'o', {Option::Type::setOutputFileName, hasArg}},
    {'e', {Option::Type::setEntrySymbolName, hasArg}},
    {'l', {Option::Type::searchForLibrary, hasArg}},
    {'L', {Option::Type::addLibrarySearchPath, hasArg}},
    {'z', {Option::Type::keyword, hasArg}},
    {'m', {Option::Type::ignore, noArg}}};

// It would be so neat to have a constexpr map, but that is not (yet) available
const std::unordered_map<std::string_view, Option> longOptionStrings{
    {"output"sv, {Option::Type::setOutputFileName, hasArg}},
    {"entry"sv, {Option::Type::setEntrySymbolName, hasArg}},
    {"library"sv, {Option::Type::searchForLibrary, hasArg}},
    {"library-path"sv, {Option::Type::addLibrarySearchPath, hasArg}},
    {"Bstatic"sv, {Option::Type::disableSharedLinking, noArg}},
    {"non_shared"sv, {Option::Type::disableSharedLinking, noArg}},
    {"dn"sv, {Option::Type::disableSharedLinking, noArg}},
    {"static"sv, {Option::Type::disableSharedLinking, noArg}},
    {"Bdynamic"sv, {Option::Type::enableSharedLinking, noArg}},
    {"dy"sv, {Option::Type::enableSharedLinking, noArg}},
    {"call_shared"sv, {Option::Type::enableSharedLinking, noArg}},
    {"push_state"sv, {Option::Type::pushBState, noArg}},
    {"pop_state"sv, {Option::Type::popBState, noArg}},
    {"eh-frame-hdr"sv, {Option::Type::enableEhFrameHdr, noArg}},
    {"no-eh-frame-hdr"sv, {Option::Type::disableEhFrameHdr, noArg}},
    {"build-id"sv, {Option::Type::buildID, hasArg}},
    {"start-group"sv, {Option::Type::ignore, noArg}},
    {"end-group"sv, {Option::Type::ignore, noArg}},
    {"plugin"sv, {Option::Type::ignore, hasArg}},
    {"plugin-opt"sv, {Option::Type::ignore, hasArg}},
    {"add-needed"sv, {Option::Type::ignore, noArg}},
    {"no-add-needed"sv, {Option::Type::ignore, noArg}},
    {"as-needed"sv, {Option::Type::ignore, noArg}},
    {"no-as-needed"sv, {Option::Type::ignore, noArg}},
    {"dynamic-linker"sv, {Option::Type::ignore, hasArg}},
    {"no-dynamic-linker"sv, {Option::Type::ignore, noArg}},
    {"nostdlib"sv, {Option::Type::ignore, noArg}},
    {"hash-style"sv, {Option::Type::ignore, hasArg}}};

struct SplitArgIntoOptionAndParam {
    struct {
        std::string_view arg;
        readonly_span<char*> argv;
        size_t argIndex;
    } in;

    struct {
        out<Option> option;
        out<std::string_view> param;
        out<size_t> numArgsConsumedByParameter;
    } out;
};

// For some reason, arguments for ld are really inconsistent
// There are short and long arguments
// The short arguments start with - and are a single letter
// followed by their argument either directly after or in the next argument string
// The long arguments can start with both - and --
// The long arguments can also take an option directly in the same string by using = ...
// or by taking it in as the next string in the argument vector
// There is probably someway to get getopt_long to do the right thing, but at this point it's
auto splitArgIntoOptionAndParam(SplitArgIntoOptionAndParam p) -> void {
    auto& [arg, argv, argIndex] = p.in;
    auto& [option, param, numArgsConsumedByParameter] = p.out;
    numArgsConsumedByParameter = 0;

    auto parseLongOption = [&](std::string_view longOption) {
        auto separator = longOption.find('=');
        if (separator == longOption.npos) {
            numArgsConsumedByParameter = 1;
            // the linter sees argIndex as a pointer value... but it's a size_t?
            // NOLINTNEXTLINE
            if ((argIndex + 1) < argv.size()) { 
                param = argv[argIndex + 1];
            } else {
                param = ""sv;
            }
        } else {
            param = longOption.substr(separator + 1);
            longOption = longOption.substr(0, separator);
        }
        auto longOptIt = longOptionStrings.find(longOption);
        option = (longOptIt != longOptionStrings.end()) ? longOptIt->second : Option{Option::Type::unrecognized};
    };

    if (arg[1] == '-') {
        // Definitely a long option
        arg.remove_prefix(2);
        return parseLongOption(arg);
    }

    auto it = shortOptions.find(arg[1]);
    if (it == shortOptions.end()) {
        arg.remove_prefix(1);
        return parseLongOption(arg);
    }
    option = it->second;
    if (arg.length() > 2) {
        param = arg.substr(2);
    } else {
        param = argv[argIndex + 1];
        numArgsConsumedByParameter = 1;
    }
    return;
}

} // namespace

auto argumentsToLinkerParameters(parametersFor::ArgumentsToLinkerParameters p) -> StatusCode {
    auto& [argv] = p.in;
    auto& [linkerOptions,
           inputFilePaths,
           libraryPathMemory] = p.out;
    inputFilePaths.reserve(argv.size());

    // default values, just to be sure
    linkerOptions.outputFileName = "a.out";
    linkerOptions.entrySymbolName = "_start";
    linkerOptions.createEhFrameHeader = false;

    enum class BState : uint8_t {
        bDynamic = 0,
        bStatic = 1
    };
    BState currentBState = BState::bDynamic;
    std::vector<BState> bStateStack{};

    struct LibSearches {
        size_t filenameIndex;
        BState bState;
    };
    std::vector<LibSearches> librariesToFind;
    std::vector<std::string_view> librarySearchPaths;

    for (size_t i = 0; i < argv.size(); ++i) {
        std::string_view arg{argv[i]};
        if (arg.length() < 2 || !arg.starts_with('-')) {
            inputFilePaths.push_back(arg);
            continue;
        }
        if (arg == "--"sv) break;

        Option option;
        std::string_view param{""};
        size_t numSkippedParameters{0};
        splitArgIntoOptionAndParam({.in{arg, argv, i},
                                    .out{option, param, numSkippedParameters}});

        if (option.hasArg)
            i += numSkippedParameters;

        // match option with action
        switch (option.type) {
            using enum Option::Type;

            case setOutputFileName: {
                linkerOptions.outputFileName = param;
            } break;
            case setEntrySymbolName: {
                linkerOptions.entrySymbolName = param;
            } break;
            case searchForLibrary: {
                librariesToFind.push_back({inputFilePaths.size(), currentBState});
                inputFilePaths.push_back(param);
            } break;
            case addLibrarySearchPath: {
                librarySearchPaths.push_back(param);
            } break;
            case disableSharedLinking: {
                currentBState = BState::bStatic;
            } break;
            case enableSharedLinking: {
                currentBState = BState::bDynamic;
            } break;
            case pushBState: {
                bStateStack.push_back(currentBState);
            } break;
            case popBState: {
                if (bStateStack.empty())
                    return report(StatusCode::not_ok, " --pop-state without preceding --push-state");
                bStateStack.pop_back();
            } break;
            case enableEhFrameHdr: {
                linkerOptions.createEhFrameHeader = true;
            } break;
            case disableEhFrameHdr: {
                linkerOptions.createEhFrameHeader = false;
            } break;
            case buildID: {
                auto hasOption = arg.find('=') != arg.npos;
                if (hasOption && param != "none"sv)
                    return report(StatusCode::not_ok, "unsupported build id: ", param);
            } break;
            case keyword: {
                if (param != "now"sv && param != "noexecstack" && param != "relro")
                    return report(StatusCode::not_ok, "unsupported keyword: ", param);
            } break;
            case unrecognized: return report(StatusCode::not_ok, "unrecognized option: ", arg, " ", param);
            default: /*ignored option*/ break;
        }
    }

    // The string needs to be preserved in a vector such that the string_view always points to valid memory
    // Reserving enough space will make sure that view to strings with small buffer optimization
    // will still point to a valid place since the string won't get moved elsewhere due to a re-allocation
    for (auto [filenameIndex, bstate] : librariesToFind) {
        namespace fs = std::filesystem;

        auto addLibraryToInput = [&](std::string_view filename, std::string_view ending) {
            // This would be a great use for std::format
            // But gcc 12 doesn't support it, so here is the old way to do something like this
            std::stringstream ss;
            ss << "lib"sv << filename << ending;
            auto libName = ss.str();

            auto pathIt = std::find_if(librarySearchPaths.begin(), librarySearchPaths.end(), [&](std::string_view pathName) {
                return fs::exists(fs::path{pathName} / libName);
            });

            if (pathIt == librarySearchPaths.end())
                return false;

            // Using the filesystem api to get a correct string for the library path.
            // Should handle all edge cases at the cost of some more memory allocations then maybe necessary
            auto str = (fs::path{*pathIt} / libName).string();
            auto strLenIncludingNull = str.size() + 1;
            auto backingMemory = static_cast<char*>(libraryPathMemory.allocate(strLenIncludingNull));
            std::memcpy(backingMemory, str.c_str(), strLenIncludingNull);
            inputFilePaths[filenameIndex] = std::string_view{backingMemory, strLenIncludingNull};
            return true;
        };

        if (bstate == BState::bDynamic && addLibraryToInput(inputFilePaths[filenameIndex], ".so"sv))
            return report(StatusCode::not_ok, " can't link against a static library but found it anyway :",
                          "lib", inputFilePaths[filenameIndex], ".so");

        if (!addLibraryToInput(inputFilePaths[filenameIndex], ".a"sv))
            return report(StatusCode::not_ok, "could not find: lib", inputFilePaths[filenameIndex], ".a");
    }

    return StatusCode::ok;
}

} // namespace cppld