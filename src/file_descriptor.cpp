#include "file_descriptor.hpp"

#include <utility>

#include <unistd.h>

namespace dell1320c {

FileDescriptor::FileDescriptor(int open_descriptor) noexcept : descriptor(open_descriptor) {}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept : descriptor(std::exchange(other.descriptor, -1)) {}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
        close();
        descriptor = std::exchange(other.descriptor, -1);
    }
    return *this;
}

FileDescriptor::~FileDescriptor() {
    close();
}

void FileDescriptor::close() noexcept {
    if (descriptor >= 0) ::close(descriptor);
    descriptor = -1;
}

}
