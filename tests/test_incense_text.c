// tests/test_incense_text.c —— 焚香 UI 字符串与字体码点清单一致性（宿主可跑，
// 不依赖 LVGL）。防止清单漂移：incense_text.h 里每个字符串的每个码点都必须
// 出现在 assets/fonts/incense_chars.txt（或 ASCII 0x20-0x7E 区段）中。
// 编译：cc -std=c11 -Wall -Wextra -Werror
//       tests/test_incense_text.c -o /tmp/t 然后在仓库根目录运行 /tmp/t。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CODEPOINTS 512
#define KNOWN_MISSING 0x9F98U // 龍：故意不在清单中的负例
#define KNOWN_PRESENT 0x711AU // 焚：标题锚点，防止空清单漏检

static int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: cannot open %s (run from repository root)\n", path);
        g_failures++;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        printf("FAIL: cannot read %s\n", path);
        g_failures++;
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

// 严格 UTF-8 解码；非法序列返回 -1。
static int decode_utf8(const unsigned char *s, uint32_t *cp) {
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    int len = s[0] < 0xE0 ? 2 : (s[0] < 0xF0 ? 3 : 4);
    if (len == 2 && s[0] < 0xC2) return -1;
    uint32_t value = len == 2 ? (uint32_t)(s[0] & 0x1F)
                             : len == 3 ? (uint32_t)(s[0] & 0x0F)
                                        : (uint32_t)(s[0] & 0x07);
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) return -1;
        value = (value << 6) | (uint32_t)(s[i] & 0x3F);
    }
    if (len == 3 && value < 0x800) return -1;
    if (len == 4 && value < 0x10000) return -1;
    *cp = value;
    return len;
}

static uint32_t s_allowed[MAX_CODEPOINTS];
static size_t s_allowed_count;

static int allowed_contains(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7E) return 1; // ASCII 由 --range 隐式覆盖
    for (size_t i = 0; i < s_allowed_count; i++) {
        if (s_allowed[i] == cp) return 1;
    }
    return 0;
}

int main(void) {
    // 1) 读入清单文件（# 注释 + 每行一个 0xXXXX）。
    char *inventory = read_file("assets/fonts/incense_chars.txt");
    if (!inventory) return 1;
    for (char *line = strtok(inventory, "\n"); line;
         line = strtok(NULL, "\n")) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        uint32_t cp;
        if (sscanf(line, " 0x%8x", &cp) == 1 || sscanf(line, "0x%8x", &cp) == 1) {
            if (s_allowed_count < MAX_CODEPOINTS) {
                s_allowed[s_allowed_count++] = cp;
            }
        }
    }
    CHECK(s_allowed_count >= 20); // 空清单或截断都应失败

    // 锚点：已知存在与已知缺失。
    CHECK(allowed_contains(KNOWN_PRESENT));
    CHECK(!allowed_contains(KNOWN_MISSING));

    // 2) 解析 incense_text.h 的全部字符串常量。
    char *header = read_file("main/incense_text.h");
    if (!header) return 1;
    size_t strings = 0;
    size_t codepoints = 0;
    for (char *p = header; *p;) {
        if (strncmp(p, "#define INCENSE_TXT_", 20) != 0) {
            while (*p && *p != '\n') p++;
            if (*p) p++;
            continue;
        }
        char *quote = strchr(p + 20, '"');
        if (!quote) break;
        quote++;
        strings++;
        while (*quote && *quote != '"') {
            uint32_t cp;
            int len = decode_utf8((const unsigned char *)quote, &cp);
            if (len < 0) {
                printf("FAIL: invalid UTF-8 in incense_text.h near \"%.8s\"\n",
                       quote);
                g_failures++;
                break;
            }
            if (!allowed_contains(cp)) {
                printf("FAIL: U+%04X not in font inventory (incense_chars.txt)"
                       "\n",
                       cp);
                g_failures++;
            }
            codepoints++;
            quote += len;
        }
        p = quote;
    }
    CHECK(strings >= 10); // 字符串清单被意外清空时应失败
    CHECK(codepoints >= 30);

    free(inventory);
    free(header);
    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("incense text coverage: %zu string(s), %zu codepoint(s), all "
           "covered\n",
           strings, codepoints);
    return 0;
}
