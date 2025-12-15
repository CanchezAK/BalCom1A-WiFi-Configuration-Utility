#include "app/app_known_networks.h"

#include "platform/platform.h"

#include <glib/gstdio.h>
#include <string.h>

#define KNOWN_NETWORKS_GROUP "known_passwords"
#define KNOWN_NETWORKS_FILENAME "known_networks.ini"

#define APP_CONFIG_DIR "BalCom1A_Configuration_Utility"
#define LEGACY_CONFIG_DIR "GTK_Test"

static gchar *hex_encode_utf8(const char *s) {
  if (!s) {
    return NULL;
  }
  size_t n = strlen(s);
  gchar *out = g_malloc0(n * 2 + 1);
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    out[i * 2 + 0] = hex[(c >> 4) & 0xF];
    out[i * 2 + 1] = hex[c & 0xF];
  }
  out[n * 2] = '\0';
  return out;
}

static gboolean hex_val(char c, guint8 *out) {
  if (c >= '0' && c <= '9') {
    *out = (guint8)(c - '0');
    return TRUE;
  }
  if (c >= 'a' && c <= 'f') {
    *out = (guint8)(10 + (c - 'a'));
    return TRUE;
  }
  if (c >= 'A' && c <= 'F') {
    *out = (guint8)(10 + (c - 'A'));
    return TRUE;
  }
  return FALSE;
}

static gchar *hex_decode_to_utf8(const char *hexstr) {
  if (!hexstr) {
    return NULL;
  }
  size_t n = strlen(hexstr);
  if (n % 2 != 0) {
    return NULL;
  }
  gchar *out = g_malloc0(n / 2 + 1);
  for (size_t i = 0; i < n; i += 2) {
    guint8 hi = 0, lo = 0;
    if (!hex_val(hexstr[i], &hi) || !hex_val(hexstr[i + 1], &lo)) {
      g_free(out);
      return NULL;
    }
    out[i / 2] = (char)((hi << 4) | lo);
  }
  out[n / 2] = '\0';
  return out;
}

static gchar *make_key_for_ssid(const char *ssid) {
  gchar *hex = hex_encode_utf8(ssid);
  if (!hex) {
    return NULL;
  }
  gchar *key = g_strdup_printf("ssid_%s", hex);
  g_free(hex);
  return key;
}

static gboolean parse_ssid_from_key(const char *key, gchar **out_ssid) {
  if (!key || !out_ssid) {
    return FALSE;
  }
  *out_ssid = NULL;
  const char *prefix = "ssid_";
  if (g_str_has_prefix(key, prefix) == FALSE) {
    return FALSE;
  }
  const char *hex = key + strlen(prefix);
  gchar *ssid = hex_decode_to_utf8(hex);
  if (!ssid || ssid[0] == '\0') {
    g_free(ssid);
    return FALSE;
  }
  *out_ssid = ssid;
  return TRUE;
}

static gchar *build_user_config_path(void) {
  const char *base = g_get_user_config_dir();
  if (!base) {
    return NULL;
  }
  return g_build_filename(base, APP_CONFIG_DIR, KNOWN_NETWORKS_FILENAME, NULL);
}

static gchar *build_legacy_user_config_path(void) {
  const char *base = g_get_user_config_dir();
  if (!base) {
    return NULL;
  }
  return g_build_filename(base, LEGACY_CONFIG_DIR, KNOWN_NETWORKS_FILENAME, NULL);
}

static gchar *build_default_save_path(void) {
  /* Prefer user config dir (writable). */
  return build_user_config_path();
}

static gchar *find_existing_load_path(void) {
  /* Prefer next-to-exe if present (portable mode), otherwise user config dir. */
  gchar *p0 = platform_build_path_next_to_exe(KNOWN_NETWORKS_FILENAME);
  if (p0 && g_file_test(p0, G_FILE_TEST_EXISTS)) {
    return p0;
  }
  g_free(p0);

  gchar *p1 = build_user_config_path();
  if (p1 && g_file_test(p1, G_FILE_TEST_EXISTS)) {
    return p1;
  }
  g_free(p1);

  /* Backward compatibility: older builds stored it under LEGACY_CONFIG_DIR. */
  gchar *p2 = build_legacy_user_config_path();
  if (p2 && g_file_test(p2, G_FILE_TEST_EXISTS)) {
    return p2;
  }
  g_free(p2);
  return NULL;
}

static void ensure_known_table(AppState *st) {
  if (!st) {
    return;
  }
  if (!st->knownPasswords) {
    st->knownPasswords = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }
}

const char *app_known_networks_lookup(AppState *st, const char *ssid) {
  if (!st || !ssid || !st->knownPasswords) {
    return NULL;
  }
  return (const char*)g_hash_table_lookup(st->knownPasswords, ssid);
}

gboolean app_known_networks_load(AppState *st) {
  if (!st) {
    return FALSE;
  }
  ensure_known_table(st);

  gchar *path = find_existing_load_path();
  if (!path) {
    DBG_LOG(st, "[KNOWN] No known networks file found\n");
    return TRUE;
  }

  GKeyFile *kf = g_key_file_new();
  GError *err = NULL;
  gboolean ok = g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err);
  if (!ok) {
    DBG_LOG(st, "[KNOWN] Load failed (%s): %s\n", path, err ? err->message : "unknown");
    g_clear_error(&err);
    g_key_file_free(kf);
    g_free(path);
    return FALSE;
  }

  g_hash_table_remove_all(st->knownPasswords);

  gsize n_keys = 0;
  gchar **keys = g_key_file_get_keys(kf, KNOWN_NETWORKS_GROUP, &n_keys, NULL);
  if (keys) {
    for (gsize i = 0; i < n_keys; i++) {
      gchar *ssid = NULL;
      if (!parse_ssid_from_key(keys[i], &ssid)) {
        continue;
      }
      gchar *pwd = g_key_file_get_string(kf, KNOWN_NETWORKS_GROUP, keys[i], NULL);
      if (pwd && pwd[0] != '\0') {
        g_hash_table_replace(st->knownPasswords, ssid, pwd);
      } else {
        g_free(ssid);
        g_free(pwd);
      }
    }
    g_strfreev(keys);
  }

  DBG_LOG(st, "[KNOWN] Loaded known networks from %s\n", path);
  g_key_file_free(kf);
  g_free(path);
  return TRUE;
}

gboolean app_known_networks_save(AppState *st) {
  if (!st) {
    return FALSE;
  }
  if (!st->knownPasswords) {
    return TRUE;
  }

  gchar *path = build_default_save_path();
  if (!path) {
    return FALSE;
  }

  gchar *dir = g_path_get_dirname(path);
  if (dir) {
    (void)g_mkdir_with_parents(dir, 0700);
  }

  GKeyFile *kf = g_key_file_new();

  GHashTableIter it;
  gpointer k = NULL;
  gpointer v = NULL;
  g_hash_table_iter_init(&it, st->knownPasswords);
  while (g_hash_table_iter_next(&it, &k, &v)) {
    const char *ssid = (const char*)k;
    const char *pwd = (const char*)v;
    if (!ssid || !pwd || pwd[0] == '\0') {
      continue;
    }
    gchar *key = make_key_for_ssid(ssid);
    if (!key) {
      continue;
    }
    g_key_file_set_string(kf, KNOWN_NETWORKS_GROUP, key, pwd);
    g_free(key);
  }

  gsize out_len = 0;
  gchar *data = g_key_file_to_data(kf, &out_len, NULL);
  gboolean ok = FALSE;
  if (data) {
    GError *err = NULL;
    ok = g_file_set_contents(path, data, (gssize)out_len, &err);
    if (!ok) {
      DBG_LOG(st, "[KNOWN] Save failed (%s): %s\n", path, err ? err->message : "unknown");
      g_clear_error(&err);
    } else {
      DBG_LOG(st, "[KNOWN] Saved known networks to %s\n", path);
    }
  }

  g_free(data);
  g_key_file_free(kf);
  g_free(dir);
  g_free(path);
  return ok;
}

void app_known_networks_remember(AppState *st, const char *ssid, const char *password) {
  if (!st || !ssid || !password || ssid[0] == '\0' || password[0] == '\0') {
    return;
  }
  ensure_known_table(st);
  g_hash_table_replace(st->knownPasswords, g_strdup(ssid), g_strdup(password));
  (void)app_known_networks_save(st);
}

void app_known_networks_forget(AppState *st, const char *ssid) {
  if (!st || !ssid || !st->knownPasswords) {
    return;
  }
  (void)g_hash_table_remove(st->knownPasswords, ssid);
  (void)app_known_networks_save(st);
}
