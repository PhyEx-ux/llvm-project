; A3 negative: AS1 -> AS4 is the reverse of an unapproved relation.
define ptr addrspace(4) @f(ptr addrspace(1) %p) {
  %q = addrspacecast ptr addrspace(1) %p to ptr addrspace(4)
  ret ptr addrspace(4) %q
}
