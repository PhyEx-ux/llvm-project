; A3 negative: AS4 -> AS9 (far RAM) is not an approved conversion.
define ptr addrspace(9) @f(ptr addrspace(4) %p) {
  %q = addrspacecast ptr addrspace(4) %p to ptr addrspace(9)
  ret ptr addrspace(9) %q
}
