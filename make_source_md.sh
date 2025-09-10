#!/usr/bin/env bash

OUTPUT_FILE="source_files.md"
CURRENT_DIR=$(pwd)

# Очищаем выходной файл
: > "$OUTPUT_FILE"

# Заголовок документа
echo -e "# Список C++ файлов и их содержимое\n" >> "$OUTPUT_FILE"

# Обработка файлов с пробелами в именах
find . -type f \( -name "*.cpp" -o -name "*.h" \) -print0 | while IFS= read -r -d $'\0' filepath; do
    # Нормализация пути
    normalized_path=$(realpath --relative-to="$CURRENT_DIR" "$filepath")
    
    # Заголовок раздела
    echo -e "## Файл: ${normalized_path}\n" >> "$OUTPUT_FILE"
    
    # Проверка существования файла
    if [ ! -f "$filepath" ]; then
        echo -e "!!! ОШИБКА: Файл не найден\n" >> "$OUTPUT_FILE"
        continue
    fi
    
    # Блок с содержимым
    {
        echo '```cpp'
        cat "$filepath"
        echo -e '\n```\n'
        echo "---"
    } >> "$OUTPUT_FILE" 2>/dev/null || {
        echo -e "!!! ОШИБКА: Не удалось прочитать файл\n" >> "$OUTPUT_FILE"
    }
    
    echo -e "\n" >> "$OUTPUT_FILE"
done

echo "Документ создан: file://${CURRENT_DIR}/${OUTPUT_FILE}"
