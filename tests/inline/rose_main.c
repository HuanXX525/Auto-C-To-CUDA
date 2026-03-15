#include <stdio.h>
void func();
int arr[5] = {(0), (0), (0), (0), (0)};

int main()
{
  for (int i = 0; i < 5; i++) {{
      arr[0] = 0;
      rose_inline_end__2:
      ;
    }
  }
  for (int i = 0; i < 5; i++) {
    printf("%d ",arr[i]);
  }
  return 0;
}

void func()
{
  arr[0] = 0;
}
