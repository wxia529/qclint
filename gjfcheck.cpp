#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct CheckResult {
    bool ok;
    std::string message;
};

struct TextLine {
    std::string text;
    std::string ending;
};

static std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

static bool is_gjf_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) &&
           to_lower(path.extension().string()) == ".gjf";
}

static std::vector<fs::path> collect_gjf_files(
    const std::vector<std::string>& inputs
) {
    std::set<fs::path> unique_files;

    auto add_path = [&unique_files](const fs::path& path) {
        std::error_code error;

        if (fs::is_directory(path, error)) {
            fs::directory_iterator iterator(path, error);
            if (error) {
                throw std::runtime_error(
                    "cannot open directory '" + path.string() + "': " +
                    error.message()
                );
            }

            const fs::directory_iterator end;
            while (iterator != end) {
                if (is_gjf_file(iterator->path())) {
                    unique_files.insert(
                        fs::absolute(iterator->path()).lexically_normal()
                    );
                }

                iterator.increment(error);
                if (error) {
                    throw std::runtime_error(
                        "cannot read directory '" + path.string() + "': " +
                        error.message()
                    );
                }
            }
        } else if (error) {
            throw std::runtime_error(
                "cannot inspect '" + path.string() + "': " + error.message()
            );
        } else if (is_gjf_file(path)) {
            unique_files.insert(fs::absolute(path).lexically_normal());
        } else if (!fs::exists(path, error)) {
            if (error) {
                throw std::runtime_error(
                    "cannot inspect '" + path.string() + "': " +
                    error.message()
                );
            }
            throw std::runtime_error(
                "path does not exist: '" + path.string() + "'"
            );
        }
    };

    if (inputs.empty()) {
        add_path(".");
    } else {
        for (const auto& input : inputs) {
            add_path(input);
        }
    }

    return {unique_files.begin(), unique_files.end()};
}

static std::vector<TextLine> split_lines(const std::string& contents) {
    std::vector<TextLine> lines;
    std::size_t position = 0;

    while (position < contents.size()) {
        const std::size_t newline = contents.find('\n', position);
        if (newline == std::string::npos) {
            lines.push_back({contents.substr(position), ""});
            break;
        }

        std::size_t text_end = newline;
        std::string ending = "\n";
        if (text_end > position && contents[text_end - 1] == '\r') {
            --text_end;
            ending = "\r\n";
        }

        lines.push_back({
            contents.substr(position, text_end - position),
            std::move(ending)
        });
        position = newline + 1;
    }

    return lines;
}

static bool write_replacement(
    const fs::path& destination,
    const std::vector<TextLine>& lines,
    std::string& error_message
) {
    fs::path temporary = destination;
    temporary += ".gjfcheck.tmp";

    for (unsigned int suffix = 1; fs::exists(temporary); ++suffix) {
        temporary = destination;
        temporary += ".gjfcheck.tmp." + std::to_string(suffix);
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error_message = "cannot create temporary file '" +
                            temporary.string() + "'";
            return false;
        }

        for (const auto& line : lines) {
            output << line.text << line.ending;
        }

        output.close();
        if (!output) {
            error_message = "cannot finish writing temporary file '" +
                            temporary.string() + "'";
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }

    std::error_code error;
    const fs::perms original_permissions = fs::status(destination, error).permissions();
    if (!error) {
        fs::permissions(temporary, original_permissions, error);
    }

    error.clear();
    fs::rename(temporary, destination, error);
    if (error) {
        error_message = "cannot replace '" + destination.string() + "': " +
                        error.message();
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }

    return true;
}

static CheckResult check_file(const fs::path& gjf_path, bool fix) {
    std::ifstream input(gjf_path, std::ios::binary);
    if (!input) {
        return {false, "[ERROR] " + gjf_path.string() + ": cannot read file"};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        return {false, "[ERROR] " + gjf_path.string() + ": cannot read file"};
    }

    std::vector<TextLine> lines = split_lines(buffer.str());

    // Capture the original spacing and quoting so --fix changes only the value.
    const std::regex chk_pattern(
        R"REGEX(^(\s*%chk\s*=\s*)(?:"([^"]+)"|'([^']+)'|(\S+))(\s*)$)REGEX",
        std::regex_constants::icase
    );

    std::smatch match;
    std::size_t chk_line_index = 0;
    std::string chk_value;
    std::string prefix;
    std::string suffix;
    char quote = '\0';
    bool found = false;

    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (!found && std::regex_match(lines[index].text, match, chk_pattern)) {
            prefix = match[1].str();
            if (match[2].matched) {
                chk_value = match[2].str();
                quote = '"';
            } else if (match[3].matched) {
                chk_value = match[3].str();
                quote = '\'';
            } else {
                chk_value = match[4].str();
            }
            suffix = match[5].str();
            chk_line_index = index;
            found = true;
        }
    }

    if (!found) {
        return {
            false,
            "[ERROR] " + gjf_path.filename().string() +
            ": no %chk=... line found"
        };
    }

    const std::string expected_name = gjf_path.stem().string() + ".chk";
    const std::string actual_name = fs::path(chk_value).filename().string();

    if (actual_name == expected_name) {
        return {
            true,
            "[ OK ] " + gjf_path.filename().string() +
            ": %chk=" + chk_value
        };
    }

    if (!fix) {
        return {
            false,
            "[FAIL] " + gjf_path.filename().string() +
            ": %chk=" + chk_value +
            "; expected %chk=" + expected_name
        };
    }

    const fs::path old_chk_path(chk_value);
    const fs::path new_chk_path = old_chk_path.has_parent_path()
        ? old_chk_path.parent_path() / expected_name
        : fs::path(expected_name);
    const std::string new_chk_value = new_chk_path.string();

    lines[chk_line_index].text = prefix;
    if (quote != '\0') {
        lines[chk_line_index].text += quote;
    }
    lines[chk_line_index].text += new_chk_value;
    if (quote != '\0') {
        lines[chk_line_index].text += quote;
    }
    lines[chk_line_index].text += suffix;

    fs::path write_path = gjf_path;
    std::error_code path_error;
    if (fs::is_symlink(gjf_path, path_error)) {
        write_path = fs::canonical(gjf_path, path_error);
    }
    if (path_error) {
        return {
            false,
            "[ERROR] " + gjf_path.filename().string() +
            ": cannot resolve file path: " + path_error.message()
        };
    }

    std::string write_error;
    if (!write_replacement(write_path, lines, write_error)) {
        return {
            false,
            "[ERROR] " + gjf_path.filename().string() + ": " + write_error
        };
    }

    return {
        true,
        "[FIX ] " + gjf_path.filename().string() +
        ": %chk=" + chk_value + " -> %chk=" + new_chk_value
    };
}

static void print_help(const char* program_name) {
    std::cout
        << "Check whether each Gaussian .gjf filename matches its %chk name.\n\n"
        << "Usage:\n"
        << "  " << program_name << " [PATH ...] [--fix]\n\n"
        << "Arguments:\n"
        << "  PATH       A .gjf file or a directory (current directory by default)\n"
        << "  --fix      Correct mismatched %chk names in place\n"
        << "  -h, --help Show this help message\n\n"
        << "Example:\n"
        << "  " << program_name << " *.gjf --fix\n"
        << "  AuAu-gamma-cam.gjf must contain %chk=AuAu-gamma-cam.chk\n";
}

int main(int argc, char* argv[]) {
    bool fix = false;
    bool options_enabled = true;
    std::vector<std::string> inputs;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (options_enabled && argument == "--") {
            options_enabled = false;
        } else if (options_enabled && argument == "--fix") {
            fix = true;
        } else if (options_enabled &&
                   (argument == "-h" || argument == "--help")) {
            print_help(argv[0]);
            return 0;
        } else if (options_enabled && argument.size() > 1 &&
                   argument.front() == '-') {
            std::cerr << "[ERROR] Unknown option: " << argument << '\n'
                      << "Try '" << argv[0] << " --help' for usage.\n";
            return 2;
        } else {
            inputs.push_back(argument);
        }
    }

    std::vector<fs::path> files;
    try {
        files = collect_gjf_files(inputs);
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] File search failed: " << error.what() << '\n';
        return 2;
    }

    if (files.empty()) {
        std::cerr << "No .gjf files found.\n";
        return 2;
    }

    int passed = 0;
    int failed = 0;

    for (const auto& file : files) {
        const CheckResult result = check_file(file, fix);
        std::cout << result.message << '\n';

        if (result.ok) {
            ++passed;
        } else {
            ++failed;
        }
    }

    std::cout << "\nTotal: " << files.size()
              << " .gjf file(s); passed: " << passed
              << "; failed: " << failed << '\n';

    return failed == 0 ? 0 : 1;
}
