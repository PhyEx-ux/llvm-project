; A3 negative: AS4 -> AS3 is not approved in this slice.
define ptr addrspace(3) @f(ptr addrspace(4) %p) {
  %q = addrspacecast ptr addrspace(4) %p to ptr addrspace(3)
  ret ptr addrspace(3) %q
}
