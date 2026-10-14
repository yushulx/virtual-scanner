/*
 * custom_scanner.c - SANE backend implementing a virtual (virtual = no
 * hardware) scanner that feeds images stored on disk to SANE frontends
 * (scanimage, xsane, the Dynamic Web TWAIN service, ...).
 *
 * Multi-image support (same behaviour as the Windows/macOS virtual scanners):
 *
 *   - All PNM images (P5/P6: .pgm/.ppm/.pnm) found in the configured image
 *     directory are used, sorted in natural order (2.pnm before 10.pnm).
 *   - The position inside that list is persisted, so scanning repeatedly
 *     keeps returning a *different* image, exactly like the macOS
 *     VirtualScanner cycle index (scan_cycle_index.txt).
 *   - Selecting the "Automatic Document Feeder" source (or mode=adf) makes a
 *     single scan job return every image in the set, one page per sane_start,
 *     until SANE_STATUS_NO_DOCS is returned.
 *
 * Configuration (first match wins for the file, later files override):
 *   /etc/sane.d/custom_scanner.conf
 *   $XDG_CONFIG_HOME/custom_scanner.conf  (default ~/.config/custom_scanner.conf)
 *   $CUSTOM_SCANNER_CONFIG
 * Environment variables (highest priority) override the files, see the
 * README for the full list.
 *
 * Build:
 *   make
 * See linux/README.md for installation and usage.
 */

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef CUSTOM_SCANNER_DEFAULT_DIR
/* Overridden by the Makefile with the image directory of this checkout. */
#define CUSTOM_SCANNER_DEFAULT_DIR "/usr/share/custom_scanner/images"
#endif

#define BACKEND_NAME        "custom_scanner"
#define MAX_PATH_LEN        PATH_MAX
/* The name the backend reports for its device; the SANE dll layer prefixes it
   with "custom_scanner:", so users see "custom_scanner:virtual". */
#define DEFAULT_DEVICE_NAME "virtual"
#define DEVICE_NAME_PREFIX  BACKEND_NAME ":"
#define DEFAULT_CONF_FILE   "/etc/sane.d/" BACKEND_NAME ".conf"
#define OPTION_STRING_SIZE  64

/* ------------------------------------------------------------------------- */
/* Options                                                                   */
/* ------------------------------------------------------------------------- */

enum
{
  OPT_NUM_OPTIONS = 0,
  OPT_SOURCE,
  OPT_MODE,
  OPT_COUNT
};

static const char *const source_list[] = {"Flatbed", "Automatic Document Feeder", NULL};
static const char *const mode_list[]   = {"Color", "Gray", "Binary", "Lineart", NULL};

/* Values accepted for the "source" option. */
#define SOURCE_FLATBED "Flatbed"
#define SOURCE_ADF     "Automatic Document Feeder"

/* Canonical values of the "mode" option. "Lineart" is accepted as an alias
   for "Binary" (hardware backends use either name). The color mode is
   selected by the frontend through this option, there is no configuration
   key for it. */
#define MODE_COLOR  "Color"
#define MODE_GRAY   "Gray"
#define MODE_BINARY "Binary"

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct
{
  int  width;       /* pixels per line */
  int  height;      /* number of lines */
  int  maxval;      /* 1..65535 */
  int  channels;    /* 1 (gray) or 3 (rgb) */
  int  src_bpp;     /* bytes per sample in the file: 1 or 2 */
  long data_offset; /* file offset of the first raster byte */
  long data_bytes;  /* size of the raster in the file */
  long data_read;   /* raster bytes already handed to the frontend */
} ImageInfo;

typedef struct
{
  /* Configuration */
  char image_file[MAX_PATH_LEN];  /* explicit single image, "" = use a directory */
  char image_dir[MAX_PATH_LEN];   /* directory holding the image set */
  char mode[16];                  /* auto | cycle | adf | single */
  char channel_order[8];          /* rgb (default) | gbr (legacy) */
  char scan_mode[16];             /* color mode selected by the frontend: Color | Gray | Binary */
  char state_file[MAX_PATH_LEN];  /* where the cycle position is persisted */
  char device_name[256];
  int  max_pages;                 /* 0 = every image of the set */
  int  configured_index;          /* index=..., -1 when not configured */

  /* Runtime */
  char **images;                  /* every candidate image of the current job */
  int    image_count;
  int   *pages;                   /* images[] index per page of this job */
  int    page_count;
  int    page_index;              /* -1 = no page opened yet */
  int    job_open;
  int    job_prepared;
  int    source_adf;              /* frontend selected the document feeder */
  FILE  *fp;                      /* currently opened page, NULL = none */
  ImageInfo info;
  long   page_pixel;              /* pixels already handed out of this page */
} Scanner;

static Scanner    g_scanner;
static SANE_Bool  g_initialized = SANE_FALSE;
static int        g_debug       = 0;

#define DBG(...)                                                       \
  do                                                                   \
  {                                                                    \
    if (g_debug)                                                       \
    {                                                                  \
      fprintf (stderr, BACKEND_NAME ": " __VA_ARGS__);                 \
      fputc ('\n', stderr);                                            \
    }                                                                  \
  } while (0)

#define WARN(...)                                                      \
  do                                                                   \
  {                                                                    \
    fprintf (stderr, BACKEND_NAME ": " __VA_ARGS__);                   \
    fputc ('\n', stderr);                                              \
  } while (0)

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static void set_string (char *dst, size_t size, const char *value)
{
  if (!size)
    return;
  snprintf (dst, size, "%s", value ? value : "");
}

static void trim (char *s)
{
  char *start = s;
  size_t len;

  while (*start && isspace ((unsigned char) *start))
    start++;
  if (start != s)
    memmove (s, start, strlen (start) + 1);

  len = strlen (s);
  while (len > 0 && isspace ((unsigned char) s[len - 1]))
    s[--len] = '\0';
}

static int file_exists (const char *path)
{
  struct stat st;
  return path && *path && stat (path, &st) == 0 && S_ISREG (st.st_mode);
}

static int dir_exists (const char *path)
{
  struct stat st;
  return path && *path && stat (path, &st) == 0 && S_ISDIR (st.st_mode);
}

/* Expands a leading "~" or "~/" using $HOME. */
static void expand_tilde (const char *value, char *out, size_t out_size)
{
  const char *home = getenv ("HOME");

  if (!value)
  {
    set_string (out, out_size, "");
    return;
  }

  if (value[0] == '~' && (value[1] == '\0' || value[1] == '/') && home && *home)
    snprintf (out, out_size, "%s%s", home, value + 1);
  else
    set_string (out, out_size, value);
}

static void path_join (char *out, size_t out_size, const char *dir, const char *name)
{
  size_t len;

  snprintf (out, out_size, "%s", dir ? dir : "");
  len = strlen (out);
  if (len && out[len - 1] != '/' && len + 1 < out_size)
  {
    out[len] = '/';
    out[len + 1] = '\0';
    len++;
  }
  if (name && len < out_size)
    snprintf (out + len, out_size - len, "%s", name);
}

/* mkdir -p (best effort, used for the state file). */
static void make_parent_dirs (const char *path)
{
  char buffer[MAX_PATH_LEN];
  char *p;

  set_string (buffer, sizeof buffer, path);
  for (p = buffer + 1; *p; p++)
  {
    if (*p != '/')
      continue;
    *p = '\0';
    if (mkdir (buffer, 0700) != 0 && errno != EEXIST)
      return;
    *p = '/';
  }
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

static void default_state_file (char *out, size_t out_size)
{
  const char *env = getenv ("XDG_STATE_HOME");
  const char *home = getenv ("HOME");
  char dir[MAX_PATH_LEN];

  if (env && *env)
    snprintf (dir, sizeof dir, "%s/" BACKEND_NAME, env);
  else if (home && *home)
    snprintf (dir, sizeof dir, "%s/.local/state/" BACKEND_NAME, home);
  else
  {
    /* No home directory (e.g. running as a daemon): no persistence. */
    set_string (out, out_size, "");
    return;
  }

  path_join (out, out_size, dir, "index");
}

static void config_defaults (Scanner *s)
{
  s->image_file[0] = '\0';
  set_string (s->image_dir, sizeof s->image_dir, CUSTOM_SCANNER_DEFAULT_DIR);
  set_string (s->mode, sizeof s->mode, "auto");
  set_string (s->channel_order, sizeof s->channel_order, "rgb");
  set_string (s->scan_mode, sizeof s->scan_mode, MODE_COLOR);
  set_string (s->device_name, sizeof s->device_name, DEFAULT_DEVICE_NAME);
  s->max_pages        = 0;
  s->configured_index = -1;
  default_state_file (s->state_file, sizeof s->state_file);
}

static void apply_config (Scanner *s, const char *key, const char *value)
{
  if (!strcasecmp (key, "image") || !strcasecmp (key, "file"))
    expand_tilde (value, s->image_file, sizeof s->image_file);
  else if (!strcasecmp (key, "directory") || !strcasecmp (key, "dir") || !strcasecmp (key, "folder"))
    expand_tilde (value, s->image_dir, sizeof s->image_dir);
  else if (!strcasecmp (key, "mode"))
    set_string (s->mode, sizeof s->mode, value);
  else if (!strcasecmp (key, "channel_order") || !strcasecmp (key, "channels"))
    set_string (s->channel_order, sizeof s->channel_order, value);
  else if (!strcasecmp (key, "state_file") || !strcasecmp (key, "state"))
    expand_tilde (value, s->state_file, sizeof s->state_file);
  else if (!strcasecmp (key, "device_name"))
    set_string (s->device_name, sizeof s->device_name, value);
  else if (!strcasecmp (key, "maxcount") || !strcasecmp (key, "max_pages") || !strcasecmp (key, "pages"))
    s->max_pages = atoi (value);
  else if (!strcasecmp (key, "index"))
    s->configured_index = atoi (value);
  else
    DBG ("ignoring unknown configuration key '%s'", key);
}

static int read_config_file (Scanner *s, const char *path)
{
  FILE *fp;
  char  line[1024];

  if (!path || !*path)
    return 0;

  fp = fopen (path, "r");
  if (!fp)
    return 0;

  while (fgets (line, sizeof line, fp))
  {
    char *value, *comment;

    comment = strchr (line, '#');
    if (comment)
      *comment = '\0';
    trim (line);
    if (!*line)
      continue;

    value = strpbrk (line, "= \t");
    if (!value)
    {
      DBG ("ignoring malformed configuration line '%s' in %s", line, path);
      continue;
    }
    *value++ = '\0';

    /* "key = value", "key=value" and "key value" are all accepted. */
    while (*value == '=' || *value == ' ' || *value == '\t')
      value++;

    trim (line);
    trim (value);
    if (*line)
      apply_config (s, line, value);
  }

  fclose (fp);
  DBG ("loaded configuration from %s", path);
  return 1;
}

static void user_config_file (char *out, size_t out_size)
{
  const char *env  = getenv ("XDG_CONFIG_HOME");
  const char *home = getenv ("HOME");
  char dir[MAX_PATH_LEN];

  if (env && *env)
    snprintf (dir, sizeof dir, "%s", env);
  else if (home && *home)
    snprintf (dir, sizeof dir, "%s/.config", home);
  else
  {
    set_string (out, out_size, "");
    return;
  }

  path_join (out, out_size, dir, BACKEND_NAME ".conf");
}

/* SANE prefixes the name reported by sane_get_devices with "<backend>:", so
   "custom_scanner:virtual" and "virtual" are both advertised as
   "custom_scanner:virtual"; drop a prefix that may be configured. */
static void normalize_device_name (Scanner *s)
{
  size_t length = strlen (DEVICE_NAME_PREFIX);

  if (!strncmp (s->device_name, DEVICE_NAME_PREFIX, length))
    memmove (s->device_name, s->device_name + length, strlen (s->device_name + length) + 1);
}

static void load_config (Scanner *s)
{
  const char *env;
  char        path[MAX_PATH_LEN];

  config_defaults (s);

  read_config_file (s, DEFAULT_CONF_FILE);
  user_config_file (path, sizeof path);
  read_config_file (s, path);

  env = getenv ("CUSTOM_SCANNER_CONFIG");
  if (env && *env)
  {
    expand_tilde (env, path, sizeof path);
    read_config_file (s, path);
  }

  if ((env = getenv ("CUSTOM_SCANNER_IMAGE")) && *env)
    expand_tilde (env, s->image_file, sizeof s->image_file);
  if ((env = getenv ("CUSTOM_SCANNER_IMAGE_DIR")) && *env)
    expand_tilde (env, s->image_dir, sizeof s->image_dir);
  if ((env = getenv ("CUSTOM_SCANNER_MODE")) && *env)
    set_string (s->mode, sizeof s->mode, env);
  if ((env = getenv ("CUSTOM_SCANNER_CHANNEL_ORDER")) && *env)
    set_string (s->channel_order, sizeof s->channel_order, env);
  if ((env = getenv ("CUSTOM_SCANNER_DEVICE_NAME")) && *env)
    set_string (s->device_name, sizeof s->device_name, env);
  if ((env = getenv ("CUSTOM_SCANNER_MAXCOUNT")) && *env)
    s->max_pages = atoi (env);
  if ((env = getenv ("CUSTOM_SCANNER_INDEX")) && *env)
    s->configured_index = atoi (env);
  if ((env = getenv ("CUSTOM_SCANNER_STATE_FILE")) && *env)
    expand_tilde (env, s->state_file, sizeof s->state_file);

  normalize_device_name (s);
}

/* ------------------------------------------------------------------------- */
/* The image set                                                             */
/* ------------------------------------------------------------------------- */

static int has_image_suffix (const char *name)
{
  const char *dot = strrchr (name, '.');

  if (!dot)
    return 0;
  return !strcasecmp (dot, ".pnm") || !strcasecmp (dot, ".ppm") || !strcasecmp (dot, ".pgm");
}

/* Natural order comparison so that "2.pnm" sorts before "10.pnm". */
static int image_compare (const void *a, const void *b)
{
  const char *left  = *(const char *const *) a;
  const char *right = *(const char *const *) b;

  while (*left && *right)
  {
    if (isdigit ((unsigned char) *left) && isdigit ((unsigned char) *right))
    {
      const char *l = left;
      const char *r = right;
      const char *l_end;
      const char *r_end;
      long        l_length;
      long        r_length;

      while (*l == '0')
        l++;
      while (*r == '0')
        r++;

      l_end = l;
      r_end = r;
      while (isdigit ((unsigned char) *l_end))
        l_end++;
      while (isdigit ((unsigned char) *r_end))
        r_end++;

      l_length = (long) (l_end - l);
      r_length = (long) (r_end - r);

      /* More digits means a bigger number (and no leading zeros remain). */
      if (l_length != r_length)
        return l_length < r_length ? -1 : 1;
      while (l < l_end && r < r_end)
      {
        if (*l != *r)
          return *l < *r ? -1 : 1;
        l++;
        r++;
      }

      left  = l_end;
      right = r_end;
      continue;
    }

    {
      int lc = tolower ((unsigned char) *left);
      int rc = tolower ((unsigned char) *right);

      if (lc != rc)
        return lc < rc ? -1 : 1;
    }
    left++;
    right++;
  }

  if (*left == *right)
    return 0;
  return *left ? 1 : -1;
}

static void free_image_list (Scanner *s)
{
  int i;

  for (i = 0; i < s->image_count; i++)
    free (s->images[i]);
  free (s->images);
  s->images      = NULL;
  s->image_count = 0;
}

static int add_image (Scanner *s, const char *path)
{
  char **images = realloc (s->images, (size_t) (s->image_count + 1) * sizeof (char *));

  if (!images)
    return 0;

  s->images = images;
  s->images[s->image_count] = strdup (path);
  if (!s->images[s->image_count])
    return 0;
  s->image_count++;
  return 1;
}

/* Builds s->images out of the configured single image or image directory. */
static SANE_Status build_image_list (Scanner *s)
{
  const char *dir;
  DIR        *folder;
  struct dirent *entry;

  free_image_list (s);

  if (s->image_file[0])
  {
    if (!file_exists (s->image_file))
    {
      WARN ("configured image '%s' does not exist", s->image_file);
      return SANE_STATUS_NO_DOCS;
    }
    add_image (s, s->image_file);
    return s->image_count ? SANE_STATUS_GOOD : SANE_STATUS_NO_MEM;
  }

  dir = s->image_dir[0] ? s->image_dir : CUSTOM_SCANNER_DEFAULT_DIR;
  if (!dir_exists (dir))
  {
    WARN ("image directory '%s' does not exist", dir);
    return SANE_STATUS_NO_DOCS;
  }

  folder = opendir (dir);
  if (!folder)
  {
    WARN ("cannot open image directory '%s': %s", dir, strerror (errno));
    return SANE_STATUS_IO_ERROR;
  }

  while ((entry = readdir (folder)) != NULL)
  {
    char path[MAX_PATH_LEN];

    if (entry->d_name[0] == '.' || !has_image_suffix (entry->d_name))
      continue;
    path_join (path, sizeof path, dir, entry->d_name);
    if (!add_image (s, path))
    {
      closedir (folder);
      free_image_list (s);
      return SANE_STATUS_NO_MEM;
    }
  }
  closedir (folder);

  if (s->image_count > 1)
    qsort (s->images, (size_t) s->image_count, sizeof (char *), image_compare);

  DBG ("%d image(s) in '%s'", s->image_count, dir);
  return s->image_count ? SANE_STATUS_GOOD : SANE_STATUS_NO_DOCS;
}

/* ------------------------------------------------------------------------- */
/* Cycle position (persisted between scan jobs)                              */
/* ------------------------------------------------------------------------- */

static int load_state (const Scanner *s)
{
  FILE *fp;
  int   value = -1;

  if (!s->state_file[0])
    return -1;

  fp = fopen (s->state_file, "r");
  if (!fp)
    return -1;
  if (fscanf (fp, "%d", &value) != 1)
    value = -1;
  fclose (fp);
  return value;
}

static void save_state (const Scanner *s, int value)
{
  FILE *fp;

  if (!s->state_file[0])
    return;

  make_parent_dirs (s->state_file);
  fp = fopen (s->state_file, "w");
  if (!fp)
  {
    DBG ("cannot persist the cycle position in %s (read-only?), continuing", s->state_file);
    return;
  }
  fprintf (fp, "%d\n", value);
  fclose (fp);
}

/* ------------------------------------------------------------------------- */
/* PNM (P5 / P6) parsing                                                     */
/* ------------------------------------------------------------------------- */

/* Reads the next whitespace delimited token, skipping '#' comments. */
static int pnm_token (FILE *fp, char *buffer, size_t buffer_size)
{
  int    c;
  size_t length = 0;

  for (;;)
  {
    c = fgetc (fp);
    if (c == EOF)
      return 0;
    if (c == '#')
    {
      while ((c = fgetc (fp)) != EOF && c != '\n')
        ;
      continue;
    }
    if (isspace (c))
      continue;
    break;
  }

  while (c != EOF && !isspace (c))
  {
    if (length + 1 < buffer_size)
      buffer[length++] = (char) c;
    c = fgetc (fp);
  }
  if (c != EOF)
    ungetc (c, fp);

  buffer[length] = '\0';
  return length > 0;
}

static int pnm_integer (FILE *fp, long *value)
{
  char token[32];
  char *end = NULL;

  if (!pnm_token (fp, token, sizeof token))
    return 0;

  *value = strtol (token, &end, 10);
  return end && *end == '\0';
}

static SANE_Status parse_pnm (FILE *fp, const char *path, ImageInfo *info)
{
  char           magic[8];
  long           width = 0, height = 0, maxval = 0;
  long long      data_bytes;
  int            c;

  memset (info, 0, sizeof *info);

  if (!pnm_token (fp, magic, sizeof magic))
  {
    WARN ("'%s' is not a PNM file (cannot read the magic number)", path);
    return SANE_STATUS_IO_ERROR;
  }

  if (strcmp (magic, "P5") != 0 && strcmp (magic, "P6") != 0)
  {
    WARN ("'%s': unsupported PNM type '%s' (convert it with: convert input.png -depth 8 output.pnm)",
          path, magic);
    return SANE_STATUS_UNSUPPORTED;
  }

  if (!pnm_integer (fp, &width) || !pnm_integer (fp, &height) || !pnm_integer (fp, &maxval))
  {
    WARN ("'%s' has an invalid PNM header", path);
    return SANE_STATUS_IO_ERROR;
  }

  if (width <= 0 || height <= 0 || maxval <= 0 || maxval > 65535)
  {
    WARN ("'%s' has unsupported PNM dimensions (%ldx%ld, maxval %ld)", path, width, height, maxval);
    return SANE_STATUS_INVAL;
  }

  /* A single whitespace byte separates the header from the raster. */
  c = fgetc (fp);
  if (c == EOF)
  {
    WARN ("'%s' is truncated (no raster data)", path);
    return SANE_STATUS_IO_ERROR;
  }
  if (c == '\r')
  {
    c = fgetc (fp);
    if (c != '\n' && c != EOF)
      ungetc (c, fp);
  }

  data_bytes = (long long) width * height * (magic[1] == '6' ? 3 : 1) * (maxval > 255 ? 2 : 1);
  if (data_bytes > (long long) LONG_MAX)
  {
    WARN ("'%s' is too large to be scanned", path);
    return SANE_STATUS_INVAL;
  }

  info->width       = (int) width;
  info->height      = (int) height;
  info->maxval      = (int) maxval;
  info->channels    = (magic[1] == '6') ? 3 : 1;
  info->src_bpp     = (maxval > 255) ? 2 : 1; /* 16 bit files are downsized to 8 bit */
  info->data_offset = ftell (fp);
  info->data_bytes  = (long) data_bytes;
  info->data_read   = 0;

  if (info->data_offset < 0)
  {
    WARN ("'%s': cannot determine the raster offset", path);
    return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
/* Page / job handling                                                       */
/* ------------------------------------------------------------------------- */

static void close_page (Scanner *s)
{
  if (s->fp)
  {
    fclose (s->fp);
    s->fp = NULL;
  }
  memset (&s->info, 0, sizeof s->info);
}

static SANE_Status open_page (Scanner *s, const char *path)
{
  FILE       *fp;
  ImageInfo   info;
  SANE_Status status;
  long        file_size;

  close_page (s);

  fp = fopen (path, "rb");
  if (!fp)
  {
    WARN ("cannot open '%s': %s", path, strerror (errno));
    return SANE_STATUS_IO_ERROR;
  }

  status = parse_pnm (fp, path, &info);
  if (status != SANE_STATUS_GOOD)
  {
    fclose (fp);
    return status;
  }

  if (fseek (fp, 0, SEEK_END) != 0 || (file_size = ftell (fp)) < 0 ||
      file_size < info.data_offset + info.data_bytes)
  {
    WARN ("'%s' is truncated: the header announces %ldx%ld pixel bytes", path, (long) info.width,
          (long) info.height);
    fclose (fp);
    return SANE_STATUS_IO_ERROR;
  }

  if (fseek (fp, info.data_offset, SEEK_SET) != 0)
  {
    WARN ("'%s': cannot seek to the raster data", path);
    fclose (fp);
    return SANE_STATUS_IO_ERROR;
  }

  s->fp        = fp;
  s->info      = info;
  s->page_pixel = 0;
  DBG ("page %s: %dx%d, %s, maxval %d", path, info.width, info.height,
       info.channels == 3 ? "rgb" : "gray", info.maxval);
  return SANE_STATUS_GOOD;
}

static int feeder_selected (const Scanner *s)
{
  if (!strcasecmp (s->mode, "adf"))
    return 1;
  if (!strcasecmp (s->mode, "single") || !strcasecmp (s->mode, "cycle"))
    return 0;
  return s->source_adf; /* mode=auto */
}

/*
 * Prepares the page list of a scan job (called on the first sane_start of the
 * job, so that options set by the frontend - the paper source in particular -
 * are taken into account) and advances the persistent cycle position.
 */
static SANE_Status prepare_job (Scanner *s)
{
  SANE_Status status;
  int         start;
  int         pages;
  int         i;
  int         single = !strcasecmp (s->mode, "single");

  s->job_prepared = 1;
  s->page_index   = -1;
  s->page_count   = 0;

  status = build_image_list (s);
  if (status != SANE_STATUS_GOOD)
  {
    WARN ("no images to scan; check the directory option in %s", DEFAULT_CONF_FILE);
    return status;
  }

  if (s->configured_index >= 0)
    start = s->configured_index % s->image_count;
  else
  {
    start = load_state (s);
    if (start < 0)
      start = 0;
    start %= s->image_count;
  }

  pages = feeder_selected (s) ? s->image_count : 1;
  if (s->max_pages > 0 && pages > s->max_pages)
    pages = s->max_pages;
  if (pages < 1)
    pages = 1;

  s->pages = calloc ((size_t) pages, sizeof (int));
  if (!s->pages)
    return SANE_STATUS_NO_MEM;

  for (i = 0; i < pages; i++)
    s->pages[i] = (start + i) % s->image_count;
  s->page_count = pages;

  /* The next job continues after the images handed out by this one; only
     mode=single always returns the same image. */
  if (s->configured_index < 0 && !single)
    save_state (s, (start + pages) % s->image_count);

  DBG ("new job: %d page(s) starting at image %d (%s)", pages, start,
       feeder_selected (s) ? "document feeder" : "flatbed");
  return SANE_STATUS_GOOD;
}

/* ------------------------------------------------------------------------- */
/* SANE API                                                                  */
/* ------------------------------------------------------------------------- */

SANE_Status sane_custom_scanner_init (SANE_Int *version_code, SANE_Auth_Callback authorize)
{
  const char *env;

  (void) authorize;

  if (version_code)
    *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, 0);

  memset (&g_scanner, 0, sizeof g_scanner);
  g_scanner.page_index = -1;

  g_debug = 0;
  env     = getenv ("CUSTOM_SCANNER_DEBUG");
  if (env && *env && strcmp (env, "0") != 0)
    g_debug = 1;

  load_config (&g_scanner);
  g_initialized = SANE_TRUE;

  DBG ("init: image directory '%s', mode '%s'", g_scanner.image_dir, g_scanner.mode);
  return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_get_devices (const SANE_Device ***device_list, SANE_Bool local_only)
{
  static const SANE_Device *devices[2];
  static SANE_Device        device;

  (void) local_only;

  device.name   = g_scanner.device_name[0] ? g_scanner.device_name : DEFAULT_DEVICE_NAME;
  device.vendor = "Custom";
  device.model  = "Virtual Scanner";
  device.type   = "virtual scanner";

  devices[0] = &device;
  devices[1] = NULL;

  if (device_list)
    *device_list = devices;
  return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_open (SANE_String_Const name, SANE_Handle *handle)
{
  Scanner *s = &g_scanner;

  if (!g_initialized || !handle)
    return SANE_STATUS_INVAL;

  DBG ("open '%s'", name ? name : "");

  close_page (s);
  free (s->pages);
  s->pages        = NULL;
  s->page_count   = 0;
  s->page_index   = -1;
  s->source_adf   = SANE_FALSE;

  /* Re-read the configuration so that edits apply without restarting the
     frontend (and without touching the persisted cycle position). */
  load_config (s);

  s->job_open     = 1;
  s->job_prepared = 0;

  *handle = s;
  return SANE_STATUS_GOOD;
}

void sane_custom_scanner_close (SANE_Handle handle)
{
  Scanner *s = &g_scanner;

  (void) handle;
  DBG ("close");

  close_page (s);
  free (s->pages);
  s->pages        = NULL;
  s->page_count   = 0;
  s->page_index   = -1;
  s->job_open     = 0;
  s->job_prepared = 0;
  free_image_list (s);
}

const SANE_Option_Descriptor *sane_custom_scanner_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  static const SANE_Option_Descriptor descriptors[OPT_COUNT] = {
      {
          SANE_NAME_NUM_OPTIONS,
          SANE_TITLE_NUM_OPTIONS,
          SANE_DESC_NUM_OPTIONS,
          SANE_TYPE_INT,
          SANE_UNIT_NONE,
          sizeof (SANE_Int),
          SANE_CAP_SOFT_DETECT,
          SANE_CONSTRAINT_NONE,
          {NULL},
      },
      {
          SANE_NAME_SCAN_SOURCE,
          SANE_TITLE_SCAN_SOURCE,
          SANE_DESC_SCAN_SOURCE,
          SANE_TYPE_STRING,
          SANE_UNIT_NONE,
          OPTION_STRING_SIZE,
          SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT,
          SANE_CONSTRAINT_STRING_LIST,
          {source_list},
      },
      {
          SANE_NAME_SCAN_MODE,
          SANE_TITLE_SCAN_MODE,
          SANE_DESC_SCAN_MODE,
          SANE_TYPE_STRING,
          SANE_UNIT_NONE,
          OPTION_STRING_SIZE,
          SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT,
          SANE_CONSTRAINT_STRING_LIST,
          {mode_list},
      },
  };

  (void) handle;

  if (option < 0 || option >= OPT_COUNT)
    return NULL;
  return &descriptors[option];
}

SANE_Status sane_custom_scanner_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action,
                                                void *value, SANE_Int *info)
{
  Scanner *s = &g_scanner;

  (void) handle;

  if (info)
    *info = 0;

  switch (option)
  {
  case OPT_NUM_OPTIONS:
    if (action == SANE_ACTION_GET_VALUE && value)
    {
      *(SANE_Int *) value = OPT_COUNT;
      return SANE_STATUS_GOOD;
    }
    return SANE_STATUS_UNSUPPORTED;

  case OPT_SOURCE:
    if (action == SANE_ACTION_GET_VALUE && value)
    {
      set_string ((char *) value, OPTION_STRING_SIZE, s->source_adf ? SOURCE_ADF : SOURCE_FLATBED);
      return SANE_STATUS_GOOD;
    }
    if (action == SANE_ACTION_SET_VALUE && value)
    {
      const char *requested = (const char *) value;

      if (!strcasecmp (requested, SOURCE_ADF) || !strcasecmp (requested, "ADF") ||
          !strcasecmp (requested, "ADF Front"))
      {
        s->source_adf = SANE_TRUE;
        DBG ("document feeder selected");
        return SANE_STATUS_GOOD;
      }
      if (!strcasecmp (requested, SOURCE_FLATBED))
      {
        s->source_adf = SANE_FALSE;
        DBG ("flatbed selected");
        return SANE_STATUS_GOOD;
      }
      WARN ("unsupported source '%s' (use '%s' or '%s')", requested, SOURCE_FLATBED, SOURCE_ADF);
      return SANE_STATUS_INVAL;
    }
    return SANE_STATUS_UNSUPPORTED;

  case OPT_MODE:
    if (action == SANE_ACTION_GET_VALUE && value)
    {
      set_string ((char *) value, OPTION_STRING_SIZE, s->scan_mode);
      return SANE_STATUS_GOOD;
    }
    if (action == SANE_ACTION_SET_VALUE && value)
    {
      const char *requested = (const char *) value;

      if (!strcasecmp (requested, MODE_COLOR) || !strcasecmp (requested, "Colour"))
        set_string (s->scan_mode, sizeof s->scan_mode, MODE_COLOR);
      else if (!strcasecmp (requested, MODE_GRAY) || !strcasecmp (requested, "Grey"))
        set_string (s->scan_mode, sizeof s->scan_mode, MODE_GRAY);
      else if (!strcasecmp (requested, MODE_BINARY) || !strcasecmp (requested, "Lineart"))
        set_string (s->scan_mode, sizeof s->scan_mode, MODE_BINARY);
      else
      {
        WARN ("unsupported color mode '%s' (use Color, Gray or Binary)", requested);
        return SANE_STATUS_INVAL;
      }
      DBG ("color mode '%s' selected", s->scan_mode);
      if (info)
        *info = SANE_INFO_RELOAD_PARAMS;
      return SANE_STATUS_GOOD;
    }
    return SANE_STATUS_UNSUPPORTED;

  default:
    return SANE_STATUS_INVAL;
  }
}

/* The color mode the frontend selected through the "mode" option. */
typedef enum
{
  OUT_COLOR = 0,
  OUT_GRAY,
  OUT_BINARY
} OutMode;

static OutMode out_mode (const Scanner *s)
{
  if (!strcasecmp (s->scan_mode, MODE_GRAY))
    return OUT_GRAY;
  if (!strcasecmp (s->scan_mode, MODE_BINARY))
    return OUT_BINARY;
  return OUT_COLOR;
}

SANE_Status sane_custom_scanner_get_parameters (SANE_Handle handle, SANE_Parameters *params)
{
  Scanner *s     = &g_scanner;
  OutMode  out   = out_mode (s);
  int      depth = (out == OUT_BINARY) ? 1 : 8;

  (void) handle;

  if (!params)
    return SANE_STATUS_INVAL;
  if (!s->fp)
  {
    DBG ("get_parameters called without a started page");
    return SANE_STATUS_INVAL;
  }

  memset (params, 0, sizeof *params);
  params->format         = (out == OUT_COLOR) ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
  params->last_frame     = SANE_TRUE;
  params->bytes_per_line = (out == OUT_COLOR)   ? s->info.width * 3
                           : (out == OUT_GRAY)  ? s->info.width
                                                : (s->info.width + 7) / 8;
  params->pixels_per_line = s->info.width;
  params->lines           = s->info.height;
  params->depth           = depth;

  return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_start (SANE_Handle handle)
{
  Scanner *s = &g_scanner;

  (void) handle;

  if (!s->job_open)
  {
    WARN ("sane_start called before sane_open");
    return SANE_STATUS_INVAL;
  }

  if (!s->job_prepared)
  {
    SANE_Status status = prepare_job (s);

    if (status != SANE_STATUS_GOOD)
    {
      s->job_prepared = 0; /* let the next sane_start try again */
      return status;
    }
  }

  if (s->fp)
  {
    if (s->info.data_read < s->info.data_bytes)
    {
      /* Restart the page that is currently being transferred. */
      if (fseek (s->fp, s->info.data_offset, SEEK_SET) != 0)
        return SANE_STATUS_IO_ERROR;
      s->info.data_read = 0;
      s->page_pixel     = 0;
      return SANE_STATUS_GOOD;
    }
    close_page (s); /* page complete: move on to the next one */
  }

  s->page_index++;
  if (s->page_index >= s->page_count)
  {
    /* The job is exhausted: the next sane_start begins a new scan job, which
       picks up the image set where this one left off. */
    DBG ("no more pages in this job");
    s->page_index   = -1;
    s->job_prepared = 0;
    return SANE_STATUS_NO_DOCS;
  }

  return open_page (s, s->images[s->pages[s->page_index]]);
}

/* Legacy channel order: the RGB triples are rotated by one channel
   (RGB -> GBR) inside the read loop when channel_order=gbr. Only RGB frames
   are rotated; the default channel_order=rgb hands out plain RGB as the
   SANE standard requires. */

SANE_Status sane_custom_scanner_read (SANE_Handle handle, SANE_Byte *data, SANE_Int max_length,
                                      SANE_Int *length)
{
  Scanner *s = &g_scanner;
  OutMode  out;
  int      out_channels;
  int      bpl;
  int      legacy_gbr;
  long     src_pixel_bytes;
  long     available_pixels;
  long     want_pixels;
  long     src_bytes;
  long     got;
  long     written    = 0;
  long     last_byte  = -1;
  long     i;
  unsigned char *buffer;

  (void) handle;

  if (!length)
    return SANE_STATUS_INVAL;
  *length = 0;

  if (!s->fp || !data || max_length <= 0)
    return SANE_STATUS_EOF;
  if (s->info.data_read >= s->info.data_bytes)
    return SANE_STATUS_EOF;

  out             = out_mode (s);
  out_channels    = (out == OUT_COLOR) ? 3 : 1;
  legacy_gbr      = (s->channel_order[0] != 'r');
  src_pixel_bytes = s->info.channels * s->info.src_bpp;

  available_pixels = (s->info.data_bytes - s->info.data_read) / src_pixel_bytes;
  want_pixels      = (out == OUT_BINARY) ? (long) max_length * 8 : max_length / out_channels;
  if (want_pixels > available_pixels)
    want_pixels = available_pixels;
  if (want_pixels <= 0)
    return SANE_STATUS_EOF;

  src_bytes = want_pixels * src_pixel_bytes;
  buffer    = malloc ((size_t) src_bytes);
  if (!buffer)
    return SANE_STATUS_NO_MEM;

  got = (long) fread (buffer, 1, (size_t) src_bytes, s->fp);
  if (got <= 0)
  {
    free (buffer);
    return SANE_STATUS_EOF;
  }
  got -= got % src_pixel_bytes; /* never split a pixel */
  if (got <= 0)
  {
    free (buffer);
    return SANE_STATUS_EOF;
  }
  s->info.data_read += got;
  want_pixels = got / src_pixel_bytes;

  bpl = (s->info.width + 7) / 8;

  for (i = 0; i < want_pixels; i++)
  {
    unsigned int sample[3] = {0, 0, 0};
    unsigned int rgb[3];
    unsigned int gray;
    int          src_channels = s->info.channels;
    int          c;

    /* Read one source pixel and scale every sample to 8 bit. */
    for (c = 0; c < src_channels; c++)
    {
      const long offset = i * src_pixel_bytes + c * s->info.src_bpp;

      if (s->info.src_bpp == 2)
        sample[c] = ((unsigned int) buffer[offset] << 8) | buffer[offset + 1];
      else
        sample[c] = buffer[offset];

      sample[c] = (sample[c] * 255u + (unsigned int) s->info.maxval / 2u) / (unsigned int) s->info.maxval;
    }

    if (src_channels == 3)
    {
      rgb[0] = sample[0];
      rgb[1] = sample[1];
      rgb[2] = sample[2];
      /* ITU-R BT.601 luminance; invariant under the channel rotation. */
      gray = (rgb[0] * 299u + rgb[1] * 587u + rgb[2] * 114u) / 1000u;
    }
    else
    {
      rgb[0] = rgb[1] = rgb[2] = sample[0];
      gray = sample[0];
    }

    switch (out)
    {
    case OUT_COLOR:
      if (src_channels == 3 && legacy_gbr)
      {
        data[written++] = (SANE_Byte) rgb[1];
        data[written++] = (SANE_Byte) rgb[2];
        data[written++] = (SANE_Byte) rgb[0];
      }
      else
      {
        data[written++] = (SANE_Byte) rgb[0];
        data[written++] = (SANE_Byte) rgb[1];
        data[written++] = (SANE_Byte) rgb[2];
      }
      break;

    case OUT_GRAY:
      data[written++] = (SANE_Byte) gray;
      break;

    case OUT_BINARY:
    {
      /* 1 bit per pixel, MSB first, lines padded to whole bytes. */
      long pixel    = s->page_pixel + i;
      long line     = pixel / s->info.width;
      long column   = pixel % s->info.width;
      long byte_pos = line * bpl + column / 8;
      int  bit      = 7 - (int) (column % 8);

      if (byte_pos != last_byte)
      {
        if (byte_pos > last_byte + 1)
          memset (data + last_byte + 1, 0, (size_t) (byte_pos - last_byte - 1));
        data[byte_pos] = 0;
        last_byte      = byte_pos;
      }
      if (gray >= 128)
        data[byte_pos] |= (SANE_Byte) (1u << bit);
      written = last_byte + 1;
      break;
    }
    }
  }
  free (buffer);

  s->page_pixel += want_pixels;
  *length = (SANE_Int) written;
  return *length > 0 ? SANE_STATUS_GOOD : SANE_STATUS_EOF;
}

void sane_custom_scanner_cancel (SANE_Handle handle)
{
  Scanner *s = &g_scanner;

  (void) handle;
  DBG ("cancel");

  close_page (s);
  s->page_index = s->page_count; /* drop the rest of the job */
}

void sane_custom_scanner_exit (void)
{
  Scanner *s = &g_scanner;

  DBG ("exit");

  close_page (s);
  free (s->pages);
  s->pages = NULL;
  free_image_list (s);
  s->page_count   = 0;
  s->page_index   = -1;
  s->job_open     = 0;
  s->job_prepared = 0;
  g_initialized   = SANE_FALSE;
}
