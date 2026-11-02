#pragma once

namespace dell1320c {

class FileDescriptor {
public:
    FileDescriptor() noexcept = default;
    explicit FileDescriptor(int open_descriptor) noexcept;
    FileDescriptor(FileDescriptor&& other) noexcept;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept;
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    ~FileDescriptor();

    [[nodiscard]] int get() const noexcept {
        return descriptor;
    }

private:
    void close() noexcept;

    int descriptor = -1;
};

}
