; A3 negative: under a 16-bit AS0 model the AS4 -> AS0 cast would narrow the
; 32-bit CODE pointer to a 16-bit near pointer; rejected, never truncated.
define ptr @f(ptr addrspace(4) %p) {
  %q = addrspacecast ptr addrspace(4) %p to ptr
  ret ptr %q
}
