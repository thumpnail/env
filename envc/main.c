#ifdef _WIN32

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_SUGGESTIONS 8

typedef enum Scope {
    SCOPE_LOCAL,
    SCOPE_GLOBAL,
    SCOPE_ALL
} Scope;

typedef struct Options {
    Scope scope;
    int force_global;
} Options;

typedef struct NameList {
    char **items;
    size_t count;
    size_t cap;
} NameList;

typedef struct Entry {
    char *name;
    char *local;
    char *global;
} Entry;

typedef struct EntryList {
    Entry *items;
    size_t count;
    size_t cap;
} EntryList;

static const char *USER_ENV_KEY = "Environment";
static const char *MACHINE_ENV_KEY = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

static void print_usage(void);
static int fail(const char *message);
static Scope parse_scope_value(const char *value, int allow_all, int *ok);
static int parse_options(int argc, char **argv, Scope default_scope, int allow_all_scope, int allow_force_global, Options *out);
static HKEY scope_to_hive(Scope scope);
static const char *scope_to_subkey(Scope scope);
static const char *scope_to_name(Scope scope);
static int get_registry_string(Scope scope, const char *name, char **out_value);
static int set_registry_string(Scope scope, const char *name, const char *value);
static int unset_registry_value(Scope scope, const char *name);
static void broadcast_env_change(void);
static int list_scope(Scope scope);
static int list_all_scopes(void);
static int collect_names_from_scope(Scope scope, NameList *names);
static int append_unique_name(NameList *names, const char *name);
static void free_name_list(NameList *names);
static char *dup_str(const char *s);
static int ci_starts_with(const char *s, const char *prefix);
static int ci_contains(const char *s, const char *needle);
static void print_suggestions(const char *input, Scope scope);
static int handle_get(int argc, char **argv);
static int handle_set(int argc, char **argv);
static int handle_unset(int argc, char **argv);
static int handle_list(int argc, char **argv);
static int handle_tui(int argc, char **argv);
static void trim_newline(char *s);
static void prompt_line(const char *label, char *buffer, size_t size);
static int parse_tui_scope_choice(const char *raw, Scope *out);
static int append_entry(EntryList *entries, const char *name, const char *local, const char *global);
static Entry *find_entry(EntryList *entries, const char *name);
static void free_entries(EntryList *entries);
static int list_all_entries(EntryList *entries);

int main(int argc, char **argv)
{
    const char *command;

    if (argc <= 1) {
        return handle_tui(0, NULL);
    }

    command = argv[1];

    if (_stricmp(command, "get") == 0) {
        return handle_get(argc - 2, argv + 2);
    }

    if (_stricmp(command, "set") == 0) {
        return handle_set(argc - 2, argv + 2);
    }

    if (_stricmp(command, "unset") == 0) {
        return handle_unset(argc - 2, argv + 2);
    }

    if (_stricmp(command, "list") == 0) {
        return handle_list(argc - 2, argv + 2);
    }

    if (_stricmp(command, "tui") == 0) {
        return handle_tui(argc - 2, argv + 2);
    }

    if (_stricmp(command, "help") == 0 || _stricmp(command, "--help") == 0 || _stricmp(command, "-h") == 0) {
        print_usage();
        return 0;
    }

    return fail("Unknown command. Use: envc help");
}

static void print_usage(void)
{
    puts("envc - C99 environment variable manager (Windows)");
    puts("");
    puts("Commands:");
    puts("  envc get <name> [--scope local|global|all]");
    puts("  envc set <name> <value> [--scope local|global] [--force-global]");
    puts("  envc unset <name> [--scope local|global] [--force-global]");
    puts("  envc list [--scope local|global|all]");
    puts("  envc tui");
    puts("");
    puts("Scopes:");
    puts("  local  = current user variables (HKCU\\Environment)");
    puts("  global = machine variables (HKLM\\...\\Environment)");
    puts("  all    = read from both");
    puts("");
    puts("Safety:");
    puts("  global writes require --force-global and Administrator rights");
}

static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

static Scope parse_scope_value(const char *value, int allow_all, int *ok)
{
    *ok = 1;

    if (_stricmp(value, "local") == 0) {
        return SCOPE_LOCAL;
    }

    if (_stricmp(value, "global") == 0) {
        return SCOPE_GLOBAL;
    }

    if (_stricmp(value, "all") == 0 && allow_all) {
        return SCOPE_ALL;
    }

    *ok = 0;
    return SCOPE_LOCAL;
}

static int parse_options(int argc, char **argv, Scope default_scope, int allow_all_scope, int allow_force_global, Options *out)
{
    int i;
    out->scope = default_scope;
    out->force_global = 0;

    for (i = 0; i < argc; i++) {
        if (_stricmp(argv[i], "--scope") == 0) {
            int ok;
            if (i + 1 >= argc) {
                return fail("Missing value for --scope.");
            }

            out->scope = parse_scope_value(argv[i + 1], allow_all_scope, &ok);
            if (!ok) {
                return fail("Invalid --scope value. Use local|global|all.");
            }

            i++;
            continue;
        }

        if (_stricmp(argv[i], "--force-global") == 0) {
            if (!allow_force_global) {
                return fail("--force-global is not valid for this command.");
            }
            out->force_global = 1;
            continue;
        }

        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Unknown option: %s", argv[i]);
            return fail(msg);
        }
    }

    return 0;
}

static HKEY scope_to_hive(Scope scope)
{
    return scope == SCOPE_LOCAL ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

static const char *scope_to_subkey(Scope scope)
{
    return scope == SCOPE_LOCAL ? USER_ENV_KEY : MACHINE_ENV_KEY;
}

static const char *scope_to_name(Scope scope)
{
    return scope == SCOPE_LOCAL ? "local" : "global";
}

static int get_registry_string(Scope scope, const char *name, char **out_value)
{
    DWORD type = 0;
    DWORD size = 0;
    LONG code;
    char *buffer;

    *out_value = NULL;

    code = RegGetValueA(
        scope_to_hive(scope),
        scope_to_subkey(scope),
        name,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        &type,
        NULL,
        &size
    );

    if (code == ERROR_FILE_NOT_FOUND) {
        return 0;
    }

    if (code != ERROR_SUCCESS || size == 0) {
        return -1;
    }

    buffer = (char *)malloc(size + 1);
    if (!buffer) {
        return -1;
    }

    code = RegGetValueA(
        scope_to_hive(scope),
        scope_to_subkey(scope),
        name,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        &type,
        buffer,
        &size
    );

    if (code != ERROR_SUCCESS) {
        free(buffer);
        return -1;
    }

    buffer[size] = '\0';
    *out_value = buffer;
    return 1;
}

static int set_registry_string(Scope scope, const char *name, const char *value)
{
    HKEY key;
    LONG code;
    DWORD len = (DWORD)strlen(value) + 1;

    code = RegOpenKeyExA(scope_to_hive(scope), scope_to_subkey(scope), 0, KEY_SET_VALUE, &key);
    if (code != ERROR_SUCCESS) {
        return -1;
    }

    code = RegSetValueExA(key, name, 0, REG_EXPAND_SZ, (const BYTE *)value, len);
    RegCloseKey(key);

    if (code != ERROR_SUCCESS) {
        return -1;
    }

    broadcast_env_change();
    return 0;
}

static int unset_registry_value(Scope scope, const char *name)
{
    HKEY key;
    LONG code;

    code = RegOpenKeyExA(scope_to_hive(scope), scope_to_subkey(scope), 0, KEY_SET_VALUE, &key);
    if (code != ERROR_SUCCESS) {
        return -1;
    }

    code = RegDeleteValueA(key, name);
    RegCloseKey(key);

    if (code == ERROR_FILE_NOT_FOUND) {
        return 0;
    }

    if (code != ERROR_SUCCESS) {
        return -1;
    }

    broadcast_env_change();
    return 1;
}

static void broadcast_env_change(void)
{
    DWORD result = 0;
    SendMessageTimeoutA(
        HWND_BROADCAST,
        WM_SETTINGCHANGE,
        0,
        (LPARAM)"Environment",
        SMTO_ABORTIFHUNG,
        3000,
        &result
    );
}

static int list_scope(Scope scope)
{
    HKEY key;
    LONG code;
    DWORD index = 0;
    char name[32767];
    DWORD name_len;
    DWORD type;
    BYTE data[32767];
    DWORD data_len;

    code = RegOpenKeyExA(scope_to_hive(scope), scope_to_subkey(scope), 0, KEY_READ, &key);
    if (code != ERROR_SUCCESS) {
        fprintf(stderr, "Failed to open %s environment registry key.\n", scope_to_name(scope));
        return 1;
    }

    while (1) {
        name_len = (DWORD)(sizeof(name) - 1);
        data_len = (DWORD)(sizeof(data) - 1);

        code = RegEnumValueA(key, index, name, &name_len, NULL, &type, data, &data_len);
        if (code == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (code == ERROR_MORE_DATA) {
            index++;
            continue;
        }

        if (code != ERROR_SUCCESS) {
            RegCloseKey(key);
            return 1;
        }

        name[name_len] = '\0';
        data[data_len] = '\0';

        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            printf("%s=%s\n", name, (const char *)data);
        }

        index++;
    }

    RegCloseKey(key);
    return 0;
}

static int append_unique_name(NameList *names, const char *name)
{
    size_t i;

    for (i = 0; i < names->count; i++) {
        if (_stricmp(names->items[i], name) == 0) {
            return 0;
        }
    }

    if (names->count == names->cap) {
        size_t next_cap = names->cap == 0 ? 64 : names->cap * 2;
        char **next_items = (char **)realloc(names->items, next_cap * sizeof(char *));
        if (!next_items) {
            return -1;
        }
        names->items = next_items;
        names->cap = next_cap;
    }

    names->items[names->count] = dup_str(name);
    if (!names->items[names->count]) {
        return -1;
    }

    names->count++;
    return 0;
}

static int collect_names_from_scope(Scope scope, NameList *names)
{
    HKEY key;
    LONG code;
    DWORD index = 0;
    char name[32767];
    DWORD name_len;

    code = RegOpenKeyExA(scope_to_hive(scope), scope_to_subkey(scope), 0, KEY_READ, &key);
    if (code != ERROR_SUCCESS) {
        return 1;
    }

    while (1) {
        name_len = (DWORD)(sizeof(name) - 1);
        code = RegEnumValueA(key, index, name, &name_len, NULL, NULL, NULL, NULL);

        if (code == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (code == ERROR_MORE_DATA) {
            index++;
            continue;
        }

        if (code != ERROR_SUCCESS) {
            RegCloseKey(key);
            return 1;
        }

        name[name_len] = '\0';
        if (append_unique_name(names, name) != 0) {
            RegCloseKey(key);
            return 1;
        }

        index++;
    }

    RegCloseKey(key);
    return 0;
}

static void free_name_list(NameList *names)
{
    size_t i;
    for (i = 0; i < names->count; i++) {
        free(names->items[i]);
    }
    free(names->items);
    names->items = NULL;
    names->count = 0;
    names->cap = 0;
}

static char *dup_str(const char *s)
{
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

static int ci_starts_with(const char *s, const char *prefix)
{
    while (*prefix && *s) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        s++;
        prefix++;
    }
    return *prefix == '\0';
}

static int ci_contains(const char *s, const char *needle)
{
    size_t slen = strlen(s);
    size_t nlen = strlen(needle);
    size_t i;

    if (nlen == 0) {
        return 1;
    }

    if (nlen > slen) {
        return 0;
    }

    for (i = 0; i + nlen <= slen; i++) {
        size_t j;
        int match = 1;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)s[i + j]) != tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            return 1;
        }
    }

    return 0;
}

static void print_suggestions(const char *input, Scope scope)
{
    NameList names = {0};
    int printed = 0;
    size_t i;

    if (scope == SCOPE_LOCAL || scope == SCOPE_ALL) {
        collect_names_from_scope(SCOPE_LOCAL, &names);
    }

    if (scope == SCOPE_GLOBAL || scope == SCOPE_ALL) {
        collect_names_from_scope(SCOPE_GLOBAL, &names);
    }

    for (i = 0; i < names.count && printed < MAX_SUGGESTIONS; i++) {
        if (ci_starts_with(names.items[i], input)) {
            if (printed == 0) {
                puts("Did you mean:");
            }
            printf("  %s\n", names.items[i]);
            printed++;
        }
    }

    if (printed == 0) {
        for (i = 0; i < names.count && printed < MAX_SUGGESTIONS; i++) {
            if (ci_contains(names.items[i], input)) {
                if (printed == 0) {
                    puts("Did you mean:");
                }
                printf("  %s\n", names.items[i]);
                printed++;
            }
        }
    }

    free_name_list(&names);
}

static int handle_get(int argc, char **argv)
{
    Options options;
    const char *name;

    if (argc < 1) {
        return fail("Usage: envc get <name> [--scope local|global|all]");
    }

    name = argv[0];

    if (parse_options(argc - 1, argv + 1, SCOPE_ALL, 1, 0, &options) != 0) {
        return 1;
    }

    if (options.scope == SCOPE_LOCAL || options.scope == SCOPE_GLOBAL) {
        char *value = NULL;
        int state = get_registry_string(options.scope, name, &value);
        if (state == 1) {
            puts(value);
            free(value);
            return 0;
        }

        if (state == 0) {
            printf("%s not found in %s scope.\n", name, scope_to_name(options.scope));
            print_suggestions(name, options.scope);
            return 1;
        }

        return fail("Failed to read environment variable.");
    }

    {
        char *local = NULL;
        char *global = NULL;
        int local_state = get_registry_string(SCOPE_LOCAL, name, &local);
        int global_state = get_registry_string(SCOPE_GLOBAL, name, &global);

        if (local_state == 1 || global_state == 1) {
            printf("local:  %s\n", local ? local : "-");
            printf("global: %s\n", global ? global : "-");
            free(local);
            free(global);
            return 0;
        }

        free(local);
        free(global);
        printf("%s not found in local or global scope.\n", name);
        print_suggestions(name, SCOPE_ALL);
        return 1;
    }
}

static int handle_set(int argc, char **argv)
{
    Options options;
    const char *name;
    const char *value;

    if (argc < 2) {
        return fail("Usage: envc set <name> <value> [--scope local|global] [--force-global]");
    }

    name = argv[0];
    value = argv[1];

    if (parse_options(argc - 2, argv + 2, SCOPE_LOCAL, 0, 1, &options) != 0) {
        return 1;
    }

    if (options.scope == SCOPE_GLOBAL && !options.force_global) {
        return fail("Refusing to write global variable without --force-global.");
    }

    if (set_registry_string(options.scope, name, value) != 0) {
        if (options.scope == SCOPE_GLOBAL) {
            return fail("Global write failed. Run terminal as Administrator.");
        }
        return fail("Failed to write environment variable.");
    }

    printf("Set %s variable '%s'.\n", scope_to_name(options.scope), name);
    return 0;
}

static int handle_unset(int argc, char **argv)
{
    Options options;
    const char *name;
    int state;

    if (argc < 1) {
        return fail("Usage: envc unset <name> [--scope local|global] [--force-global]");
    }

    name = argv[0];

    if (parse_options(argc - 1, argv + 1, SCOPE_LOCAL, 0, 1, &options) != 0) {
        return 1;
    }

    if (options.scope == SCOPE_GLOBAL && !options.force_global) {
        return fail("Refusing to remove global variable without --force-global.");
    }

    state = unset_registry_value(options.scope, name);
    if (state < 0) {
        if (options.scope == SCOPE_GLOBAL) {
            return fail("Global delete failed. Run terminal as Administrator.");
        }
        return fail("Failed to remove environment variable.");
    }

    if (state == 0) {
        printf("%s not found in %s scope.\n", name, scope_to_name(options.scope));
        return 1;
    }

    printf("Unset %s variable '%s'.\n", scope_to_name(options.scope), name);
    return 0;
}

static int append_entry(EntryList *entries, const char *name, const char *local, const char *global)
{
    Entry *target;

    target = find_entry(entries, name);
    if (target) {
        if (local) {
            free(target->local);
            target->local = dup_str(local);
            if (!target->local) {
                return 1;
            }
        }
        if (global) {
            free(target->global);
            target->global = dup_str(global);
            if (!target->global) {
                return 1;
            }
        }
        return 0;
    }

    if (entries->count == entries->cap) {
        size_t next_cap = entries->cap == 0 ? 64 : entries->cap * 2;
        Entry *next_items = (Entry *)realloc(entries->items, next_cap * sizeof(Entry));
        if (!next_items) {
            return 1;
        }
        entries->items = next_items;
        entries->cap = next_cap;
    }

    entries->items[entries->count].name = dup_str(name);
    entries->items[entries->count].local = local ? dup_str(local) : NULL;
    entries->items[entries->count].global = global ? dup_str(global) : NULL;

    if (!entries->items[entries->count].name) {
        return 1;
    }

    entries->count++;
    return 0;
}

static Entry *find_entry(EntryList *entries, const char *name)
{
    size_t i;
    for (i = 0; i < entries->count; i++) {
        if (_stricmp(entries->items[i].name, name) == 0) {
            return &entries->items[i];
        }
    }
    return NULL;
}

static int list_all_entries(EntryList *entries)
{
    HKEY key;
    LONG code;
    DWORD index;

    index = 0;
    code = RegOpenKeyExA(HKEY_CURRENT_USER, USER_ENV_KEY, 0, KEY_READ, &key);
    if (code == ERROR_SUCCESS) {
        while (1) {
            char name[32767];
            BYTE data[32767];
            DWORD name_len = (DWORD)(sizeof(name) - 1);
            DWORD data_len = (DWORD)(sizeof(data) - 1);
            DWORD type;

            code = RegEnumValueA(key, index, name, &name_len, NULL, &type, data, &data_len);
            if (code == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (code == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                name[name_len] = '\0';
                data[data_len] = '\0';
                if (append_entry(entries, name, (const char *)data, NULL) != 0) {
                    RegCloseKey(key);
                    return 1;
                }
            }
            index++;
        }
        RegCloseKey(key);
    }

    index = 0;
    code = RegOpenKeyExA(HKEY_LOCAL_MACHINE, MACHINE_ENV_KEY, 0, KEY_READ, &key);
    if (code == ERROR_SUCCESS) {
        while (1) {
            char name[32767];
            BYTE data[32767];
            DWORD name_len = (DWORD)(sizeof(name) - 1);
            DWORD data_len = (DWORD)(sizeof(data) - 1);
            DWORD type;

            code = RegEnumValueA(key, index, name, &name_len, NULL, &type, data, &data_len);
            if (code == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (code == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                name[name_len] = '\0';
                data[data_len] = '\0';
                if (append_entry(entries, name, NULL, (const char *)data) != 0) {
                    RegCloseKey(key);
                    return 1;
                }
            }
            index++;
        }
        RegCloseKey(key);
    }

    return 0;
}

static void free_entries(EntryList *entries)
{
    size_t i;
    for (i = 0; i < entries->count; i++) {
        free(entries->items[i].name);
        free(entries->items[i].local);
        free(entries->items[i].global);
    }
    free(entries->items);
    entries->items = NULL;
    entries->count = 0;
    entries->cap = 0;
}

static int list_all_scopes(void)
{
    EntryList entries = {0};
    size_t i;

    if (list_all_entries(&entries) != 0) {
        free_entries(&entries);
        return fail("Failed to enumerate environment variables.");
    }

    for (i = 0; i < entries.count; i++) {
        printf("%s | local=%s | global=%s\n",
            entries.items[i].name,
            entries.items[i].local ? entries.items[i].local : "-",
            entries.items[i].global ? entries.items[i].global : "-");
    }

    free_entries(&entries);
    return 0;
}

static int handle_list(int argc, char **argv)
{
    Options options;

    if (parse_options(argc, argv, SCOPE_ALL, 1, 0, &options) != 0) {
        return 1;
    }

    if (options.scope == SCOPE_ALL) {
        return list_all_scopes();
    }

    return list_scope(options.scope);
}

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void prompt_line(const char *label, char *buffer, size_t size)
{
    printf("%s", label);
    if (fgets(buffer, (int)size, stdin) == NULL) {
        buffer[0] = '\0';
        return;
    }
    trim_newline(buffer);
}

static int parse_tui_scope_choice(const char *raw, Scope *out)
{
    if (_stricmp(raw, "local") == 0 || strcmp(raw, "1") == 0) {
        *out = SCOPE_LOCAL;
        return 1;
    }
    if (_stricmp(raw, "global") == 0 || strcmp(raw, "2") == 0) {
        *out = SCOPE_GLOBAL;
        return 1;
    }
    if (_stricmp(raw, "all") == 0 || strcmp(raw, "3") == 0) {
        *out = SCOPE_ALL;
        return 1;
    }
    return 0;
}

static int handle_tui(int argc, char **argv)
{
    Scope active_scope = SCOPE_ALL;
    char line[64];

    (void)argc;
    (void)argv;

    puts("envc interactive mode");
    puts("Type one of: list, get, set, unset, scope, help, quit");

    while (1) {
        printf("\n[scope=%s] > ", active_scope == SCOPE_LOCAL ? "local" : active_scope == SCOPE_GLOBAL ? "global" : "all");

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        trim_newline(line);

        if (_stricmp(line, "quit") == 0 || _stricmp(line, "exit") == 0) {
            break;
        }

        if (_stricmp(line, "help") == 0) {
            puts("Commands:");
            puts("  list         - list variables in active scope");
            puts("  get          - get one variable");
            puts("  set          - set variable (all scope writes to local)");
            puts("  unset        - delete variable (all scope deletes from local)");
            puts("  scope        - choose local/global/all");
            puts("  quit         - exit interactive mode");
            continue;
        }

        if (_stricmp(line, "scope") == 0) {
            char scope_raw[32];
            Scope next;
            prompt_line("Choose scope [1=local, 2=global, 3=all]: ", scope_raw, sizeof(scope_raw));
            if (parse_tui_scope_choice(scope_raw, &next)) {
                active_scope = next;
            } else {
                puts("Invalid scope.");
            }
            continue;
        }

        if (_stricmp(line, "list") == 0) {
            if (active_scope == SCOPE_ALL) {
                list_all_scopes();
            } else {
                list_scope(active_scope);
            }
            continue;
        }

        if (_stricmp(line, "get") == 0) {
            char name[256];
            char *value = NULL;

            prompt_line("Name: ", name, sizeof(name));
            if (name[0] == '\0') {
                puts("Name is required.");
                continue;
            }

            if (active_scope == SCOPE_ALL) {
                char *lv = NULL;
                char *gv = NULL;
                int ls = get_registry_string(SCOPE_LOCAL, name, &lv);
                int gs = get_registry_string(SCOPE_GLOBAL, name, &gv);
                if (ls == 1 || gs == 1) {
                    printf("local:  %s\n", lv ? lv : "-");
                    printf("global: %s\n", gv ? gv : "-");
                } else {
                    puts("Not found.");
                    print_suggestions(name, SCOPE_ALL);
                }
                free(lv);
                free(gv);
                continue;
            }

            if (get_registry_string(active_scope, name, &value) == 1) {
                puts(value);
                free(value);
            } else {
                puts("Not found.");
                print_suggestions(name, active_scope);
            }

            continue;
        }

        if (_stricmp(line, "set") == 0) {
            char name[256];
            char value[4096];
            Scope target = active_scope;

            prompt_line("Name: ", name, sizeof(name));
            prompt_line("Value: ", value, sizeof(value));

            if (name[0] == '\0') {
                puts("Name is required.");
                continue;
            }

            if (target == SCOPE_ALL) {
                target = SCOPE_LOCAL;
                puts("All scope is read-only for writes; using local scope.");
            }

            if (target == SCOPE_GLOBAL) {
                char confirm[8];
                prompt_line("Confirm global write (yes/no): ", confirm, sizeof(confirm));
                if (_stricmp(confirm, "yes") != 0) {
                    puts("Canceled.");
                    continue;
                }
            }

            if (set_registry_string(target, name, value) == 0) {
                puts("Saved.");
            } else {
                puts(target == SCOPE_GLOBAL ? "Global write failed. Run as Administrator." : "Write failed.");
            }

            continue;
        }

        if (_stricmp(line, "unset") == 0) {
            char name[256];
            Scope target = active_scope;
            int state;

            prompt_line("Name: ", name, sizeof(name));
            if (name[0] == '\0') {
                puts("Name is required.");
                continue;
            }

            if (target == SCOPE_ALL) {
                target = SCOPE_LOCAL;
                puts("All scope is read-only for deletes; using local scope.");
            }

            if (target == SCOPE_GLOBAL) {
                char confirm[8];
                prompt_line("Confirm global delete (yes/no): ", confirm, sizeof(confirm));
                if (_stricmp(confirm, "yes") != 0) {
                    puts("Canceled.");
                    continue;
                }
            }

            state = unset_registry_value(target, name);
            if (state > 0) {
                puts("Deleted.");
            } else if (state == 0) {
                puts("Not found.");
            } else {
                puts(target == SCOPE_GLOBAL ? "Global delete failed. Run as Administrator." : "Delete failed.");
            }

            continue;
        }

        puts("Unknown command. Type help.");
    }

    return 0;
}

#else

#include <stdio.h>

int main(void)
{
    fprintf(stderr, "envc currently supports Windows only.\n");
    return 1;
}

#endif
