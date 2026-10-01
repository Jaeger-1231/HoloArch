#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

bool raplName(const char *name) {
  constexpr char prefix[] = "intel-rapl:";
  if (std::strncmp(name, prefix, sizeof(prefix) - 1) != 0)
    return false;
  const char *digit = name + sizeof(prefix) - 1;
  if (!*digit)
    return false;
  for (; *digit; ++digit) {
    if (*digit < '0' || *digit > '9')
      return false;
  }
  return true;
}

bool readValue(int directory, const char *name, long long *value) {
  const int file = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (file < 0)
    return false;
  char buffer[64]{};
  const ssize_t size = read(file, buffer, sizeof(buffer) - 1);
  close(file);
  if (size <= 0)
    return false;
  char *end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(buffer, &end, 10);
  if (errno || end == buffer || parsed < 0 || (*end != '\n' && *end != '\0'))
    return false;
  *value = parsed;
  return true;
}

} // namespace

int main(int argc, char **) {
  if (argc != 1)
    return 2;
  const int root =
      open("/sys/class/powercap", O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  if (root < 0)
    return 1;
  DIR *entries = fdopendir(root);
  if (!entries) {
    close(root);
    return 1;
  }
  long long energy = 0;
  long long range = 0;
  bool found = false;
  while (dirent *entry = readdir(entries)) {
    if (!raplName(entry->d_name))
      continue;
    const int package =
        openat(root, entry->d_name, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (package < 0)
      continue;
    const bool valid = readValue(package, "energy_uj", &energy) &&
                       readValue(package, "max_energy_range_uj", &range);
    close(package);
    if (valid) {
      found = true;
      break;
    }
  }
  closedir(entries);
  if (!found)
    return 1;
  if (std::printf("%lld %lld\n", energy, range) < 0 || std::fflush(stdout) != 0)
    return 3;
  return 0;
}
