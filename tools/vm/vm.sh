# vm.sh: the VM configuration and lock protocol for sh scripts, from tools/vm/vmlib.py (source it: . tools/vm/vm.sh).
# Sets MS_VM (ssh target), MS_VM_KEY, MS_VM_LOCK, MS_VM_ROOT, MS_VM_PAUSE from the environment / ~/.config/ms3fork/vm.conf;
# MS_VM is empty when nothing configures it (the caller says so). Functions:
#   vm_lock <name>   wait for and take the VM lock + the local pause file (vmlib.py's protocol); nested under the same
#                    name (MS_VM_JOB) it takes nothing; returns 1 if not taken
#   vm_unlock        release what vm_lock took (nothing if nested or not taken)
# (set ROOT, the repository's root, before sourcing)
VMLIB_PY=${VMLIB_PY:-$ROOT/tools/vm/vmlib.py}
eval "$(python3 "$VMLIB_PY" conf --shell 2>/dev/null)"
VM_LOCK_NAME=""
vm_lock() {
  python3 "$VMLIB_PY" lock "$1" --poll "${VM_LOCK_POLL:-20}"
  case $? in
    0) VM_LOCK_NAME=$1;;
    3) VM_LOCK_NAME="";;
    *) return 1;;
  esac
  return 0
}
vm_unlock() {
  [ -n "$VM_LOCK_NAME" ] && python3 "$VMLIB_PY" unlock "$VM_LOCK_NAME"
  VM_LOCK_NAME=""
  return 0
}
