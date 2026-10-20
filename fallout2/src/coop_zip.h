#ifndef FALLOUT_COOP_ZIP_H_
#define FALLOUT_COOP_ZIP_H_

#include <string>

namespace fallout {

// CE: co-op: unpacks the files of a .zip (stored or deflated; also over
// 4 GB) whose names start with `prefix` into `dir`, without that prefix,
// when `want` (may be null) says so. Each file's CRC is checked; a name that
// would leave `dir` is refused. `progress`, when given, gets the bytes
// unpacked so far and the total; returning false cancels.
typedef bool(CoopZipWant)(const std::string& name);
typedef bool(CoopZipProgress)(void* data, unsigned long long done, unsigned long long total);
bool coop_zip_extract(const std::string& zipPath, const std::string& prefix, const std::string& dir, CoopZipWant* want, CoopZipProgress* progress, void* data, std::string* error);

} // namespace fallout

#endif /* FALLOUT_COOP_ZIP_H_ */
