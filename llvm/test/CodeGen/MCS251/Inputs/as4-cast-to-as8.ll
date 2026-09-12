; A3 negative: AS4 -> AS8 would narrow to a 16-bit near pointer; rejected.
define ptr addrspace(8) @f(ptr addrspace(4) %p) {
  %q = addrspacecast ptr addrspace(4) %p to ptr addrspace(8)
  ret ptr addrspace(8) %q
}
