// tests/kit_store.h —— 宿主测试用的假 kit_store.h:只有 fight_save 用到的三个 kit_sv_*(签名同 ai-passport/main/kit_store.h)。
// 实现在 test_save.c:一块内存当 saves 分区里的 "fight"/"save"。
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*kit_valid_fn)(const void *blob);
bool kit_sv_blob_load(const char *ns, const char *key, void *out, size_t size, kit_valid_fn valid, void *scratch);
bool kit_sv_blob_store(const char *ns, const char *key, const void *blob, size_t size);
bool kit_sv_ns_erase(const char *ns);
