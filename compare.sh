for f in build/release/fw.elf build/debug/fw_dbg.elf; do
	line=$(printf '=%.0s' {1..72})
	printf '\n\033[1;36m%s\033[0m\n' "$line"
	printf '\033[1;97;44m  %s  \033[0m\n' "$f"
	printf '\033[1;36m%s\033[0m\n' "$line"
	arm-none-eabi-readelf -SW "$f" | sed '/^Key to Flags/,$d'
	arm-none-eabi-readelf -lW "$f" | sed -n '/Elf file type/,$p'
done
