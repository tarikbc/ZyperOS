# plastron's build locks, sourced by the scripts that run inside the build
# container. One lock per folder of /work a build writes: two builds of the
# same output wait for each other, and builds of different outputs - the
# Pixel 2's and the Brick's side by side in make release, or the QEMU image
# beside the everyday one - still run at once. Buildroot's own download step
# already locks each package's folder in the shared dl/.
#
# flock(1), so the kernel lets the lock go when the process holding it ends,
# however it ends: a container killed halfway leaves nothing to clean up, which
# a lock that is a file or a folder of its own cannot promise. The lock is the
# file DIR.lock beside DIR, and holds who has it, for whoever waits.
#
#   . /src/scripts/lock.sh
#   plastron_lock /work/output        # exclusive: this writes the folder
#   plastron_lock /work/output -s     # shared: this only reads it
#
# Held for the rest of the shell and whatever it runs. A script it runs that
# locks the same folder finds it in PLASTRON_LOCKED and does not wait for its
# own caller. On a /work mounted read-only (make sdk reads a build without
# writing a byte of the volume) the file is opened for reading, which flock
# takes as well.

plastron_lock_fd=9

plastron_lock() {
	_dir=$1
	_mode=${2:--x}
	_lock=$_dir.lock
	case " ${PLASTRON_LOCKED:-} " in *" $_dir "*) return 0 ;; esac
	_fd=$plastron_lock_fd
	plastron_lock_fd=$((plastron_lock_fd - 1))
	if [ -w "$(dirname "$_dir")" ] && { [ ! -e "$_lock" ] || [ -w "$_lock" ]; }; then
		eval "exec $_fd>>\"\$_lock\""
		chmod 0666 "$_lock" 2>/dev/null || true
	elif [ -e "$_lock" ]; then
		eval "exec $_fd<\"\$_lock\""
	else
		echo "plastron: no $_lock to take, and nowhere to make one: going on without it" >&2
		return 0
	fi
	if ! flock -n "$_mode" "$_fd"; then
		echo "plastron: waiting for $_dir, in use by: $(cat "$_lock" 2>/dev/null || echo unknown)" >&2
		flock "$_mode" "$_fd"
	fi
	if [ "$_mode" = -x ] && [ -w "$_lock" ]; then
		printf '%s, since %s\n' "${PLASTRON_WHO:-a build}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$_lock"
	fi
	PLASTRON_LOCKED="${PLASTRON_LOCKED:-} $_dir"
	export PLASTRON_LOCKED
}
