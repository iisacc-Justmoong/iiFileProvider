#include <iiFileProviderNativeDevice.h>

int main() {
    iiFileProvider::native::Device device;
    return device.isOpen() ? 1 : 0;
}
