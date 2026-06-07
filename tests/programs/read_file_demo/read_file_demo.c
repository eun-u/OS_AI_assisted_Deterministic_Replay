#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "tests/fixtures/input.txt";
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  char buf[64];
  ssize_t n = read(fd, buf, sizeof(buf));
  if (n < 0) {
    perror("read");
    close(fd);
    return 1;
  }
  close(fd);
  printf("read_bytes=%zd\n", n);
  fwrite(buf, 1, (size_t)n, stdout);
  if (n == 0 || buf[n - 1] != '\n') putchar('\n');
  return 0;
}
