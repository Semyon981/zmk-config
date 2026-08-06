#!/usr/bin/env bash
#
# Прошивка ZMK на плату с UF2-бутлоадером (nice!nano и родня).
#
#   ./scripts/flash.sh build/left/zephyr/zmk.uf2
#
# Ждёт появления тома с меткой NICENANO (двойной ресет на плате),
# монтирует его при необходимости и копирует прошивку.
#
# Переменные окружения:
#   LABEL    метка тома           (по умолчанию NICENANO)
#   TIMEOUT  сколько ждать, сек   (по умолчанию 60)

set -euo pipefail

LABEL="${LABEL:-NICENANO}"
TIMEOUT="${TIMEOUT:-60}"

die() {
	echo "flash: $*" >&2
	exit 1
}

if [[ $# -ne 1 ]]; then
	echo "usage: ${0##*/} <файл.uf2>" >&2
	exit 2
fi

uf2="$1"
[[ -f "$uf2" ]] || die "нет файла: $uf2 — сначала собери прошивку"

# Ищем блочное устройство по метке. У UF2-бутлоадера FAT лежит прямо
# на "диске" без таблицы разделов, поэтому это будет /dev/sdX, не /dev/sdX1.
find_dev() {
	lsblk -rno PATH,LABEL | awk -v l="$LABEL" '$2 == l { print $1; exit }'
}

dev="$(find_dev)"

if [[ -z "$dev" ]]; then
	echo ">>> двойной ресет на плате — жду $LABEL (${TIMEOUT}s)"
	for ((i = 0; i < TIMEOUT; i++)); do
		sleep 1
		dev="$(find_dev)"
		if [[ -n "$dev" ]]; then
			break
		fi
	done
fi

[[ -n "$dev" ]] || die "$LABEL так и не появился"
echo ">>> устройство: $dev"

# Уже примонтирован? Иначе монтируем через udisks (без прав root).
mnt="$(findmnt -rno TARGET "$dev" || true)"

if [[ -z "$mnt" ]]; then
	mnt="$(udisksctl mount --no-user-interaction -b "$dev" |
		sed 's/^.* at //; s/\.$//')"
fi

[[ -n "$mnt" && -d "$mnt" ]] || die "не удалось смонтировать $dev"
echo ">>> точка монтирования: $mnt"

# Плата перезагружается сразу после получения последнего блока, поэтому
# cp нередко возвращает ошибку ввода-вывода уже после успешной записи.
echo ">>> $uf2 -> $mnt/"
if ! cp "$uf2" "$mnt/"; then
	echo ">>> cp вернул ошибку — обычно это плата отвалилась сразу после записи"
fi

sync
echo ">>> готово"