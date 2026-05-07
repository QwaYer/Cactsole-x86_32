#ifndef BUILTIN_H
#define BUILTIN_H

/* Запись таблицы встроенных команд: имя + указатель на реализацию. */
struct builtin_cmd {
    const char *name;
    int (*fn)(char **argv, int argc);
};

/* Главный диспетчер: ищет команду по всем группам и запускает её.
 * Возвращает код завершения команды или -1, если команда не найдена. */
int builtin_run(char **argv, int argc);

/* Универсальный поиск-и-вызов в локальной таблице группы.
 * Возвращает -1, если argv[0] не входит в таблицу. */
int builtin_table_run(const struct builtin_cmd *table,
                      char **argv, int argc);

#endif /* BUILTIN_H */
