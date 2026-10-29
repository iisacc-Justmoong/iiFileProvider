#include "File.h"
#include <iiFileProviderNativeDevice.h>
#include <QLockFile>

namespace iiFileProvider {
AtomicFileCommitResult File::publish(const std::filesystem::path& temporary,
                                    const std::filesystem::path& destination,bool overwrite) {
    try {
        const auto target=absolutePath(pathString(destination));
        (void)absolutePath(pathString(temporary));
        QLockFile lock(target+".iisacc-lock");
        if(!lock.tryLock(0))return {false,"file_busy","another file operation owns this path"};
        const auto result=native::publishFile(temporary,destination,overwrite);
        return {result.succeeded,result.diagnosticSuffix,result.message};
    }catch(const std::exception& error){return {false,"invalid_path",error.what()};}
}
} // namespace iiFileProvider
