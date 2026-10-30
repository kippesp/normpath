#include "shell_detection.h"

// Exit with the np::Shell value detected for this process's parent, so process
// tests can tell detected cmd.exe apart from the unrecognized-parent fallback.
int main()
{
  return static_cast<int>(np::detectParentShell());
}
