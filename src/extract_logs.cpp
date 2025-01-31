#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iomanip>
#include <sstream>

class ProgressBar {
private:
    const size_t width = 50;
    size_t total;
    std::chrono::steady_clock::time_point start_time;

public:
    ProgressBar(size_t total_size) : total(total_size) {
        start_time = std::chrono::steady_clock::now();
    }

    void update(size_t current) {
        float progress = static_cast<float>(current) / total;
        int pos = static_cast<int>(width * progress);

        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        
        std::cout << "\r[";
        for (int i = 0; i < width; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% "
                 << duration.count() << "s" << std::flush;
    }

    void finish() {
        std::cout << std::endl;
    }
};

class LogRetriever {
private:
    const char* filename;
    const size_t DATE_LENGTH = 10;
    const size_t BUFFER_SIZE = 16384;  // Increased buffer size for better performance
    bool verbose;

    struct DateComponents {
        int year, month, day;
    };

    DateComponents parseDateComponents(const std::string& date) {
        DateComponents dc;
        dc.year = std::stoi(date.substr(0, 4));
        dc.month = std::stoi(date.substr(5, 2));
        dc.day = std::stoi(date.substr(8, 2));
        return dc;
    }

    bool isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    bool validateDateComponents(const DateComponents& dc) {
        if (dc.year < 1900 || dc.year > 2100) return false;
        if (dc.month < 1 || dc.month > 12) return false;
        
        const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int maxDays = daysInMonth[dc.month - 1];
        if (dc.month == 2 && isLeapYear(dc.year)) maxDays = 29;
        
        return dc.day >= 1 && dc.day <= maxDays;
    }

    bool validateDate(const std::string& date) {
        if (date.length() != DATE_LENGTH ||
            date[4] != '-' || date[7] != '-') return false;
        
        try {
            DateComponents dc = parseDateComponents(date);
            return validateDateComponents(dc);
        } catch (...) {
            return false;
        }
    }

    std::pair<off_t, off_t> findDateBoundaries(char* mapped_file, size_t file_size, const std::string& target_date) {
        if (verbose) std::cout << "Searching for date boundaries..." << std::endl;
        
        off_t start = -1, end = -1;
        off_t left = 0, right = file_size - 1;

        // Improved position estimation
        off_t bytes_per_day = file_size / 365;
        off_t estimated_pos = estimatePosition(mapped_file, file_size, target_date, bytes_per_day);
        
        if (estimated_pos != -1) {
            // Wider search window for better accuracy
            left = std::max(0L, estimated_pos - bytes_per_day * 2);
            right = std::min(static_cast<off_t>(file_size - 1), estimated_pos + bytes_per_day * 2);
        }

        start = binarySearch(mapped_file, file_size, target_date, left, right, true);
        if (start != -1) {
            end = binarySearch(mapped_file, file_size, target_date, start, right, false);
        }

        if (verbose && start != -1 && end != -1) {
            std::cout << "Found logs between positions " << start << " and " << end << std::endl;
        }

        return {start, end};
    }

    off_t estimatePosition(char* mapped_file, size_t file_size, const std::string& target_date, off_t bytes_per_day) {
        try {
            // Read first valid date from file
            off_t pos = 0;
            while (pos < std::min(static_cast<size_t>(1000), file_size)) {
                if (isdigit(mapped_file[pos])) {
                    std::string first_date(mapped_file + pos, DATE_LENGTH);
                    if (validateDate(first_date)) {
                        int days_diff = calculateDaysDifference(first_date, target_date);
                        if (days_diff < 0) return 0;
                        return std::min(days_diff * bytes_per_day, static_cast<off_t>(file_size - 1));
                    }
                }
                pos++;
            }
        } catch (...) {}
        return -1;
    }

    int calculateDaysDifference(const std::string& date1, const std::string& date2) {
        struct tm tm1 = {}, tm2 = {};
        strptime(date1.c_str(), "%Y-%m-%d", &tm1);
        strptime(date2.c_str(), "%Y-%m-%d", &tm2);
        
        time_t time1 = mktime(&tm1);
        time_t time2 = mktime(&tm2);
        
        return static_cast<int>((time2 - time1) / (60 * 60 * 24));
    }

    off_t binarySearch(char* mapped_file, size_t file_size, const std::string& target_date, 
                      off_t left, off_t right, bool find_start) {
        while (left <= right) {
            off_t mid = left + (right - left) / 2;
            off_t line_start = findLineStart(mapped_file, file_size, mid);
            
            if (line_start == -1) return -1;
            
            // Validate we have enough characters to read
            if (line_start + DATE_LENGTH > file_size) {
                right = mid - 1;
                continue;
            }

            std::string current_date(mapped_file + line_start, DATE_LENGTH);
            
            if (find_start) {
                if (current_date < target_date) {
                    left = mid + 1;
                } else if (current_date > target_date) {
                    right = mid - 1;
                } else {
                    if (line_start == 0 || 
                        std::string(mapped_file + line_start - DATE_LENGTH - 1, DATE_LENGTH) != target_date) {
                        return line_start;
                    }
                    right = mid - 1;
                }
            } else {
                if (current_date < target_date) {
                    left = mid + 1;
                } else if (current_date > target_date) {
                    right = mid - 1;
                } else {
                    off_t next_line = findNextLine(mapped_file, file_size, line_start);
                    if (next_line >= file_size || 
                        std::string(mapped_file + next_line, DATE_LENGTH) != target_date) {
                        return next_line;
                    }
                    left = mid + 1;
                }
            }
        }
        return -1;
    }

    off_t findLineStart(char* mapped_file, size_t file_size, off_t pos) {
        if (pos <= 0) return 0;
        if (pos >= file_size) return -1;

        // Efficient line start search
        const off_t max_backtrack = 1000;  // Prevent excessive backtracking
        off_t start = std::max(0L, pos - max_backtrack);
        
        while (pos > start && mapped_file[pos - 1] != '\n') {
            pos--;
        }
        return pos;
    }

    off_t findNextLine(char* mapped_file, size_t file_size, off_t pos) {
        const off_t max_forward = 1000;  // Prevent excessive forward scanning
        off_t end = std::min(file_size, static_cast<size_t>(pos + max_forward));
        
        while (pos < end && mapped_file[pos] != '\n') {
            pos++;
        }
        return (pos < file_size) ? pos + 1 : file_size;
    }

    void createOutputDirectory() {
        std::filesystem::create_directories("output");
    }

public:
    LogRetriever(const char* fname, bool verbose_output = false) 
        : filename(fname), verbose(verbose_output) {}

    bool extractLogs(const std::string& target_date) {
        if (!validateDate(target_date)) {
            std::cerr << "Error: Invalid date format. Use YYYY-MM-DD" << std::endl;
            return false;
        }

        int fd = open(filename, O_RDONLY);
        if (fd == -1) {
            std::cerr << "Error: Cannot open file '" << filename << "': " 
                     << std::strerror(errno) << std::endl;
            return false;
        }

        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            std::cerr << "Error: Cannot get file size: " << std::strerror(errno) << std::endl;
            close(fd);
            return false;
        }

        if (sb.st_size == 0) {
            std::cerr << "Error: File is empty" << std::endl;
            close(fd);
            return false;
        }

        char* mapped_file = static_cast<char*>(
            mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
        
        if (mapped_file == MAP_FAILED) {
            std::cerr << "Error: Cannot map file: " << std::strerror(errno) << std::endl;
            close(fd);
            return false;
        }

        createOutputDirectory();
        std::string output_path = "output/output_" + target_date + ".txt";
        std::ofstream output_file(output_path, std::ios::binary);

        if (!output_file) {
            std::cerr << "Error: Cannot create output file: " << output_path << std::endl;
            munmap(mapped_file, sb.st_size);
            close(fd);
            return false;
        }

        auto [start_pos, end_pos] = findDateBoundaries(mapped_file, sb.st_size, target_date);

        if (start_pos == -1 || end_pos == -1) {
            std::cerr << "No logs found for date: " << target_date << std::endl;
            output_file.close();
            munmap(mapped_file, sb.st_size);
            close(fd);
            return false;
        }

        // Write matching logs with progress bar
        size_t total_bytes = end_pos - start_pos;
        ProgressBar progress(total_bytes);
        
        char buffer[BUFFER_SIZE];
        size_t bytes_written = 0;
        size_t pos = start_pos;

        while (pos < end_pos) {
            size_t chunk_size = std::min(BUFFER_SIZE, static_cast<size_t>(end_pos - pos));
            std::memcpy(buffer, mapped_file + pos, chunk_size);
            output_file.write(buffer, chunk_size);
            
            pos += chunk_size;
            bytes_written += chunk_size;
            progress.update(bytes_written);
        }

        progress.finish();
        output_file.close();
        munmap(mapped_file, sb.st_size);
        close(fd);
        
        std::cout << "Successfully extracted logs to: " << output_path << std::endl;
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " YYYY-MM-DD [-v]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  -v  Verbose output" << std::endl;
        return 1;
    }

    bool verbose = (argc == 3 && std::string(argv[2]) == "-v");
    LogRetriever retriever("test_logs.log", verbose);
    return retriever.extractLogs(argv[1]) ? 0 : 1;
}
