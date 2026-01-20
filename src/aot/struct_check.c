#include "../vm/vm.h"
#include <stdio.h>


int main() {
  printf("VMValue size: %zu\n", sizeof(VMValue));
  printf("VMValue.type offset: %zu\n", offsetof(VMValue, type));
  printf("VMValue.as offset: %zu\n", offsetof(VMValue, as));

  printf("Obj size: %zu\n", sizeof(Obj));
  printf("Obj.type offset: %zu\n", offsetof(Obj, type));

  printf("ObjString size: %zu\n", sizeof(ObjString));
  printf("ObjString.chars offset: %zu\n", offsetof(ObjString, chars));

  return 0;
}
