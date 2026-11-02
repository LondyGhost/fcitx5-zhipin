/* gtk_im_selftest.c — 自驱动 GTK3 输入法链路探针（临时诊断用，不入仓库）
 *
 * 目的：不依赖物理按键，用合成 GdkEventKey 走 GTK IM 模块 -> fcitx5 -> abc addon，
 * 观察 key 是否被消费、preedit 是否出现、是否立刻 commit。
 * 步骤：t=400ms 发送 Ctrl+Space（激活输入法），t=1000/1600ms 发送 a / b。
 */
#include <gtk/gtk.h>
#include <string.h>

static GtkIMContext *g_im = NULL;
static gboolean g_no_trigger = FALSE;
static gboolean g_ceshi = FALSE;
static const char *g_word = NULL;
static gboolean g_nav = FALSE;
static gboolean g_page = FALSE;
static gboolean g_hold = FALSE;
static GtkWidget *g_entry = NULL;
static int g_commit_count = 0;
static int g_preedit_count = 0;

static void on_preedit_changed(GtkIMContext *ctx, gpointer user_data) {
  (void)user_data;
  gchar *str = NULL;
  PangoAttrList *attr = NULL;
  gint cursor = 0;
  gtk_im_context_get_preedit_string(ctx, &str, &attr, &cursor);
  ++g_preedit_count;
  g_print("[preedit #%d] \"%s\" cursor=%d\n", g_preedit_count,
          str ? str : "", cursor);
  g_free(str);
  if (attr) {
    pango_attr_list_unref(attr);
  }
}

static void on_commit(GtkIMContext *ctx, const gchar *str, gpointer user_data) {
  (void)ctx;
  (void)user_data;
  ++g_commit_count;
  g_print("[commit #%d] \"%s\"\n", g_commit_count, str ? str : "");
}

static gboolean send_one(GdkEventType type, guint keyval,
                         GdkModifierType state) {
  GdkWindow *win = gtk_widget_get_window(g_entry);
  GdkEvent *ev = gdk_event_new(type);
  ev->key.window = g_object_ref(win);
  ev->key.send_event = TRUE;
  ev->key.time = GDK_CURRENT_TIME;
  ev->key.state = state;
  ev->key.keyval = keyval;
  ev->key.hardware_keycode = 38;
  ev->key.group = 0;
  ev->key.is_modifier = 0;
  const gboolean handled = gtk_im_context_filter_keypress(g_im, &ev->key);
  gdk_event_free(ev);
  return handled;
}

static void send_keyval(guint keyval, GdkModifierType state, const char *label) {
  if (!gtk_widget_get_window(g_entry)) {
    g_print("[key %s] no gdk window\n", label);
    return;
  }
  /* 真实键盘是 press + release 成对出现；只发 press 会漏掉释放路径的缺陷。 */
  const gboolean press = send_one(GDK_KEY_PRESS, keyval, state);
  const gboolean release = send_one(GDK_KEY_RELEASE, keyval, state);
  g_print("[key %s] press=%d release=%d preedit_count=%d commit_count=%d\n",
          label, press, release, g_preedit_count, g_commit_count);
}

static gboolean step_ctrl_space(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_space, GDK_CONTROL_MASK, "Ctrl+Space");
  return G_SOURCE_REMOVE;
}

static gboolean step_any(gpointer data) {
  const char ch = *(const char *)data;
  char label[8] = {0};
  g_snprintf(label, sizeof(label), "%c", ch);
  send_keyval(gdk_unicode_to_keyval((guint32)ch), 0, label);
  return G_SOURCE_REMOVE;
}

static gboolean step_a(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_a, 0, "a");
  return G_SOURCE_REMOVE;
}

static gboolean step_b(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_b, 0, "b");
  return G_SOURCE_REMOVE;
}

static gboolean step_right(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_Right, 0, "Right");
  return G_SOURCE_REMOVE;
}

static gboolean step_down(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_Down, 0, "Down");
  return G_SOURCE_REMOVE;
}

static gboolean step_space(gpointer data) {
  (void)data;
  send_keyval(GDK_KEY_space, 0, "space");
  return G_SOURCE_REMOVE;
}

static gboolean finish(gpointer data) {
  (void)data;
  gchar *str = NULL;
  PangoAttrList *attr = NULL;
  gint cursor = 0;
  gtk_im_context_get_preedit_string(g_im, &str, &attr, &cursor);
  g_print("[summary] preedit_updates=%d commits=%d final_preedit=\"%s\" entry_text=\"%s\"\n",
          g_preedit_count, g_commit_count, str ? str : "",
          gtk_entry_get_text(GTK_ENTRY(g_entry)));
  g_free(str);
  if (attr) {
    pango_attr_list_unref(attr);
  }
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app, gpointer data) {
  (void)data;
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "ABC IM selftest (auto)");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 160);
  g_entry = gtk_entry_new();
  gtk_container_add(GTK_CONTAINER(window), g_entry);
  gtk_widget_show_all(window);
  gtk_widget_grab_focus(g_entry);

  g_im = gtk_im_multicontext_new();
  g_signal_connect(g_im, "preedit-changed", G_CALLBACK(on_preedit_changed), NULL);
  g_signal_connect(g_im, "commit", G_CALLBACK(on_commit), NULL);
  gtk_im_context_set_client_window(g_im, gtk_widget_get_window(g_entry));
  gtk_im_context_focus_in(g_im);

  g_print("[info] im module=%s\n",
          gtk_im_multicontext_get_context_id(GTK_IM_MULTICONTEXT(g_im)));

  if (!g_no_trigger && !g_ceshi) {
    /* 组内顺序 keyboard-us -> pinyin -> abc：连按两次 Ctrl+Space 枚举到 abc */
    g_timeout_add(400, step_ctrl_space, NULL);
    g_timeout_add(900, step_ctrl_space, NULL);
    g_timeout_add(1500, step_a, NULL);
    g_timeout_add(2100, step_b, NULL);
    g_timeout_add(2700, step_space, NULL);
    g_timeout_add(3500, finish, NULL);
  } else if (g_word && g_hold) {
    /* 只输入字母并停住，便于外部截图观察组字框 */
    static char seq4[64];
    int n = (int)strlen(g_word);
    if (n > 63) {
      n = 63;
    }
    for (int i = 0; i < n; ++i) {
      seq4[i] = g_word[i];
      g_timeout_add(600 + i * 400, step_any, &seq4[i]);
    }
    g_timeout_add(600 + n * 400 + 6000, finish, NULL);
  } else if (g_word && g_page) {
    /* 输入字母 → 空格出候选 → 下方向键翻页 → 空格提交 */
    static char seq3[64];
    int n = (int)strlen(g_word);
    if (n > 63) {
      n = 63;
    }
    for (int i = 0; i < n; ++i) {
      seq3[i] = g_word[i];
      g_timeout_add(500 + i * 400, step_any, &seq3[i]);
    }
    const int base3 = 500 + n * 400;
    g_timeout_add(base3, step_space, NULL);
    g_timeout_add(base3 + 600, step_down, NULL);
    g_timeout_add(base3 + 1200, step_space, NULL);
    g_timeout_add(base3 + 1800, finish, NULL);
  } else if (g_word && g_nav) {
    /* 输入字母 → 空格出候选 → 右移高亮 → 空格提交高亮项 */
    static char seq2[64];
    int n = (int)strlen(g_word);
    if (n > 63) {
      n = 63;
    }
    for (int i = 0; i < n; ++i) {
      seq2[i] = g_word[i];
      g_timeout_add(500 + i * 400, step_any, &seq2[i]);
    }
    const int base = 500 + n * 400;
    g_timeout_add(base, step_space, NULL);
    g_timeout_add(base + 600, step_right, NULL);
    g_timeout_add(base + 1200, step_space, NULL);
    g_timeout_add(base + 1800, finish, NULL);
  } else if (g_word) {
    /* 依次输入 g_word 的字母，空格进候选，再空格提交首个候选。 */
    static char seq[64];
    int n = (int)strlen(g_word);
    if (n > 63) {
      n = 63;
    }
    for (int i = 0; i < n; ++i) {
      seq[i] = g_word[i];
      g_timeout_add(500 + i * 400, step_any, &seq[i]);
    }
    g_timeout_add(500 + n * 400, step_space, NULL);
    g_timeout_add(500 + n * 400 + 600, step_space, NULL);
    g_timeout_add(500 + n * 400 + 1200, finish, NULL);
  } else if (g_ceshi) {
    /* 逐字输入 ceshi，再按空格，验证词库链路 */
    static char seq[] = {'c', 'e', 's', 'h', 'i'};
    for (int i = 0; i < 5; ++i) {
      g_timeout_add(500 + i * 500, step_any, &seq[i]);
    }
    g_timeout_add(3200, step_space, NULL);
    g_timeout_add(4000, finish, NULL);
  } else {
    g_timeout_add(600, step_a, NULL);
    g_timeout_add(1200, step_b, NULL);
    g_timeout_add(1800, step_space, NULL);
    g_timeout_add(2600, finish, NULL);
  }
}

int main(int argc, char **argv) {
  g_no_trigger = (argc > 1 && g_strcmp0(argv[1], "notrigger") == 0);
  g_ceshi = (argc > 1 && g_strcmp0(argv[1], "ceshi") == 0);
  if (argc > 2 && g_strcmp0(argv[1], "hold") == 0) {
    g_word = argv[2];
    g_hold = TRUE;
    g_no_trigger = TRUE;
  }
  if (argc > 2 && g_strcmp0(argv[1], "page") == 0) {
    g_word = argv[2];
    g_page = TRUE;
    g_no_trigger = TRUE;
  }
  if (argc > 2 && g_strcmp0(argv[1], "nav") == 0) {
    g_word = argv[2];
    g_nav = TRUE;
    g_no_trigger = TRUE;
  }
  if (argc > 2 && g_strcmp0(argv[1], "word") == 0) {
    g_word = argv[2];
    g_no_trigger = TRUE;
  }
  g_setenv("GTK_IM_MODULE", "fcitx", TRUE);
  GtkApplication *app = gtk_application_new("org.example.AbcImSelftest",
                                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  /* 只把程序名传给 GtkApplication，避免 "notrigger" 被当成待打开文件 */
  const int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status;
}
