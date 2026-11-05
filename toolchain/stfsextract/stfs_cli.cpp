// stfs_cli: the host test tool for stfs_reader (the same code libstfsextract.so runs).
//   stfs_cli probe <package>              the JSON nativeProbe returns
//   stfs_cli extract <package> <instance> what nativeExtract does: the package's files go to
//                                         their folder under <instance> (stfs::TargetFolder)
#include <fcntl.h>

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#define open _open
#define close _close
#define OPEN_FLAGS (_O_RDONLY | _O_BINARY)
#else
#include <unistd.h>
#define OPEN_FLAGS O_RDONLY
#endif

#include "stfs_reader.h"

int main(int argc, char** argv)
{
    if (argc < 3 || (std::string(argv[1]) != "probe" && std::string(argv[1]) != "extract") ||
        (std::string(argv[1]) == "extract" && argc < 4))
    {
        fprintf(stderr, "usage: stfs_cli probe <package> | extract <package> <instance>\n");
        return 64;
    }
    const int fd = open(argv[2], OPEN_FLAGS);
    if (fd < 0)
    {
        fprintf(stderr, "can't open %s\n", argv[2]);
        return 1;
    }
    stfs::Result result;
    std::string error;
    auto package = stfs::Package::Open(fd, result, error);
    if (!package)
    {
        fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        close(fd);
        return int(result);
    }
    if (std::string(argv[1]) == "probe")
    {
        printf("%s\n", stfs::ProbeJson(*package).c_str());
        close(fd);
        return 0;
    }
    const std::string folder = stfs::TargetFolder(package->header, argv[3]);
    if (folder.empty())
    {
        printf("passed over (content type %08X)\n", package->header.contentType);
        close(fd);
        return 0;
    }
    if ((result = package->ReadTable(error)) == stfs::Result::Ok)
    {
        result = package->Extract(folder, [](uint64_t done, uint64_t total) {
            fprintf(stderr, "\r%llu / %llu MB", static_cast<unsigned long long>(done >> 20),
                static_cast<unsigned long long>(total >> 20));
            return true;
        }, error);
        fprintf(stderr, "\n");
    }
    if (result == stfs::Result::Ok && package->header.contentType == stfs::kContentMarketplace &&
        !stfs::WriteDisplayName(package->header, folder.substr(0, folder.find_last_of("/\\"))))
        result = stfs::Result::ExtractFailed;
    close(fd);
    if (result != stfs::Result::Ok)
    {
        fprintf(stderr, "%s: %s (%d)\n", argv[2], error.c_str(), int(result));
        return int(result);
    }
    printf("%s: %zu files to %s\n", package->header.contentId.c_str(), package->files.size(), folder.c_str());
    return 0;
}
