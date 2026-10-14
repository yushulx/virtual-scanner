/*
 * selftest.c - drives the custom_scanner SANE backend through its public
 * entry points, exactly like a SANE frontend would, and verifies the
 * multi-image behaviour:
 *
 *   - one image per scan job with a persistent cycle position (cycle/auto)
 *   - every image in a single multi page (ADF) job
 *   - a maximum page count per job (maxcount)
 *   - flatbed vs. document feeder selected through the "source" option
 *   - always the same image for mode=single and index=<n>
 *   - P5 (gray), P6 (rgb), 16 bit and commented PNM headers
 *   - the error paths (no images at all)
 *
 * Build and run with "make test". No scanner, no libsane runtime and no
 * ImageMagick needed: the test creates its own PNM files in a temporary
 * directory.
 */

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* The backend under test. */
extern SANE_Status sane_custom_scanner_init (SANE_Int *version_code, SANE_Auth_Callback authorize);
extern SANE_Status sane_custom_scanner_get_devices (const SANE_Device ***device_list, SANE_Bool local_only);
extern SANE_Status sane_custom_scanner_open (SANE_String_Const name, SANE_Handle *handle);
extern void sane_custom_scanner_close (SANE_Handle handle);
extern const SANE_Option_Descriptor *sane_custom_scanner_get_option_descriptor (SANE_Handle handle, SANE_Int option);
extern SANE_Status sane_custom_scanner_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action,
                                                       void *value, SANE_Int *info);
extern SANE_Status sane_custom_scanner_get_parameters (SANE_Handle handle, SANE_Parameters *params);
extern SANE_Status sane_custom_scanner_start (SANE_Handle handle);
extern SANE_Status sane_custom_scanner_read (SANE_Handle handle, SANE_Byte *data, SANE_Int max_length,
                                             SANE_Int *length);
extern void sane_custom_scanner_cancel (SANE_Handle handle);
extern void sane_custom_scanner_exit (void);

#define IMG_W        4
#define IMG_H        2
#define IMG_COUNT    4
#define MAX_PAGES    16
#define PAGE_BYTES   (IMG_W * IMG_H * 3)

static int g_failures = 0;
static int g_checks   = 0;
static char g_root[256];

#define CHECK(cond, ...)                                                       \
  do                                                                           \
  {                                                                            \
    g_checks++;                                                                \
    if (!(cond))                                                               \
    {                                                                          \
      g_failures++;                                                            \
      printf ("  FAIL (line %d): ", __LINE__);                                 \
      printf (__VA_ARGS__);                                                    \
      printf ("\n");                                                           \
    }                                                                          \
  } while (0)

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void join (char *out, size_t size, const char *dir, const char *name)
{
  snprintf (out, size, "%s/%s", dir, name);
}

static void make_dir (const char *path)
{
  if (mkdir (path, 0700) != 0)
  {
    perror (path);
    exit (2);
  }
}

/* Writes a P6 (rgb, magic6 = 1) or P5 (gray) image whose samples are
   "id + channel", so every page can be identified by its pixel values. */
static int write_image (const char *path, int magic6, int id, int maxval, int with_comment)
{
  FILE *fp = fopen (path, "wb");
  int   channels = magic6 ? 3 : 1;
  int   i, c;

  if (!fp)
  {
    perror (path);
    return 0;
  }

  fprintf (fp, "P%c\n", magic6 ? '6' : '5');
  if (with_comment)
    fprintf (fp, "# written by the custom_scanner self test\n");
  fprintf (fp, "%d %d\n%d\n", IMG_W, IMG_H, maxval);

  for (i = 0; i < IMG_W * IMG_H; i++)
  {
    for (c = 0; c < channels; c++)
    {
      unsigned int value = (unsigned int) (id + c);

      if (maxval > 255)
      {
        value *= 257; /* 16 bit: the same value scaled to maxval 65535 */
        fputc ((int) ((value >> 8) & 0xff), fp);
        fputc ((int) (value & 0xff), fp);
      }
      else
        fputc ((int) value, fp);
    }
  }

  fclose (fp);
  return 1;
}

/* Creates the default image set: IMG_COUNT rgb images, ids 0..IMG_COUNT-1. */
static void write_image_set (const char *dir)
{
  char path[600];
  int  i;

  for (i = 0; i < IMG_COUNT; i++)
  {
    char name[32];

    snprintf (name, sizeof name, "%d.pnm", i + 1);
    join (path, sizeof path, dir, name);
    if (!write_image (path, 1, i, 255, i == 0))
    {
      printf ("cannot create %s\n", path);
      exit (2);
    }
  }
}

static void reinit (void)
{
  SANE_Int version = 0;

  sane_custom_scanner_exit ();
  if (sane_custom_scanner_init (&version, NULL) != SANE_STATUS_GOOD)
  {
    printf ("sane_custom_scanner_init failed\n");
    exit (2);
  }
}

static void set_env (const char *key, const char *value)
{
  if (value)
    setenv (key, value, 1);
  else
    unsetenv (key);
}

static void reset_state (const char *name)
{
  char path[512];

  join (path, sizeof path, g_root, name);
  unlink (path);
  set_env ("CUSTOM_SCANNER_STATE_FILE", path);
}

/* ------------------------------------------------------------------------- */
/* Scan job driver                                                           */
/* ------------------------------------------------------------------------- */

typedef struct
{
  int           pages;
  int           ids[MAX_PAGES];
  long          bytes[MAX_PAGES];
  int           format[MAX_PAGES];
  int           depth[MAX_PAGES];
  int           lines[MAX_PAGES];
  int           bytes_per_line[MAX_PAGES];
  long          data_len[MAX_PAGES];
  unsigned char data[MAX_PAGES][PAGE_BYTES * 2];
  int           error;
  const char   *error_text;
} Job;

/* Scans one complete job (start/read until NO_DOCS), like "scanimage --batch". */
static void scan_job (SANE_Handle handle, Job *job, int max_pages)
{
  memset (job, 0, sizeof *job);

  for (;;)
  {
    SANE_Parameters params;
    SANE_Status     status;
    long            total = 0;

    status = sane_custom_scanner_start (handle);
    if (status == SANE_STATUS_NO_DOCS)
      return;
    if (status != SANE_STATUS_GOOD)
    {
      job->error      = 1;
      job->error_text = "sane_start";
      return;
    }
    if (job->pages >= max_pages)
    {
      job->error      = 1;
      job->error_text = "more pages than expected";
      return;
    }

    memset (&params, 0, sizeof params);
    if (sane_custom_scanner_get_parameters (handle, &params) != SANE_STATUS_GOOD)
    {
      job->error      = 1;
      job->error_text = "sane_get_parameters";
      return;
    }

    job->format[job->pages]         = (int) params.format;
    job->depth[job->pages]          = (int) params.depth;
    job->lines[job->pages]          = (int) params.lines;
    job->bytes_per_line[job->pages] = (int) params.bytes_per_line;

    for (;;)
    {
      SANE_Byte buffer[64];
      SANE_Int  length = -1;

      status = sane_custom_scanner_read (handle, buffer, (SANE_Int) sizeof buffer, &length);
      if (status == SANE_STATUS_EOF)
        break;
      if (status != SANE_STATUS_GOOD)
      {
        job->error      = 1;
        job->error_text = "sane_read";
        return;
      }
      if (length < 0 || (size_t) length > sizeof buffer)
      {
        job->error      = 1;
        job->error_text = "sane_read returned an invalid length";
        return;
      }
      if (length == 0)
        continue; /* a frontend would simply call read again */

      if (total + length <= (long) sizeof job->data[job->pages])
        memcpy (job->data[job->pages] + total, buffer, (size_t) length);
      total += length;
    }

    job->data_len[job->pages] = total < (long) sizeof job->data[job->pages]
                                    ? total
                                    : (long) sizeof job->data[job->pages];
    job->bytes[job->pages] = total;
    job->ids[job->pages]   = total > 0 ? job->data[job->pages][0] : -1;
    job->pages++;
  }
}

static void check_pages (const Job *job, const char *what, int expected_pages, const int *expected_ids)
{
  int i;

  CHECK (!job->error, "%s: %s", what, job->error_text ? job->error_text : "unexpected error");
  CHECK (job->pages == expected_pages, "%s: expected %d page(s), got %d", what, expected_pages, job->pages);

  if (job->error || job->pages != expected_pages)
    return;

  if (expected_ids)
    for (i = 0; i < expected_pages; i++)
      CHECK (job->ids[i] == expected_ids[i], "%s: page %d is image %d, expected %d", what, i, job->ids[i],
             expected_ids[i]);
}

/* Verifies the parameters and the content of an rgb page. */
static void check_rgb_content (const Job *job, const char *what, int page, int image_id, int rgb_order)
{
  long index;

  CHECK (job->format[page] == SANE_FRAME_RGB, "%s: page %d is not rgb", what, page);
  CHECK (job->depth[page] == 8, "%s: page %d has depth %d, expected 8", what, page, job->depth[page]);
  CHECK (job->lines[page] == IMG_H, "%s: page %d has %d lines, expected %d", what, page, job->lines[page], IMG_H);
  CHECK (job->bytes_per_line[page] == IMG_W * 3, "%s: page %d has bytes_per_line %d, expected %d", what, page,
         job->bytes_per_line[page], IMG_W * 3);
  CHECK (job->bytes[page] == PAGE_BYTES, "%s: page %d has %ld bytes, expected %d", what, page, job->bytes[page],
         PAGE_BYTES);

  /* Every pixel is (image_id, image_id + 1, image_id + 2); with the legacy
     "gbr" order the channels are rotated by one. */
  for (index = 0; index < job->data_len[page]; index++)
  {
    int channel  = (int) (index % 3);
    int expected = image_id + (rgb_order ? channel : (channel + 1) % 3);

    CHECK (job->data[page][index] == expected, "%s: page %d byte %ld is %d, expected %d", what, page, index,
           job->data[page][index], expected);
  }
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_cycle (void)
{
  char          images[512];
  SANE_Handle   handle = NULL;
  Job           job;
  int           expected[1];
  int           i;

  printf ("cycle mode: one image per job, position is remembered\n");

  join (images, sizeof images, g_root, "images");
  set_env ("CUSTOM_SCANNER_IMAGE", NULL);
  set_env ("CUSTOM_SCANNER_IMAGE_DIR", images);
  set_env ("CUSTOM_SCANNER_MODE", "cycle");
  set_env ("CUSTOM_SCANNER_CHANNEL_ORDER", "rgb");
  set_env ("CUSTOM_SCANNER_MAXCOUNT", NULL);
  set_env ("CUSTOM_SCANNER_INDEX", NULL);
  reset_state ("state-cycle");
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");

  for (i = 0; i <= IMG_COUNT; i++)
  {
    char what[64];

    snprintf (what, sizeof what, "job %d", i + 1);
    scan_job (handle, &job, MAX_PAGES);
    expected[0] = i % IMG_COUNT;
    check_pages (&job, what, 1, expected);
    if (job.pages == 1)
      check_rgb_content (&job, what, 0, expected[0], 1);
  }

  sane_custom_scanner_close (handle);
}

static void test_adf_maxcount (void)
{
  char        images[512];
  SANE_Handle handle = NULL;
  Job         job;
  int         expected[2];
  int         i;

  printf ("adf mode with maxcount=2: multi page jobs that continue where the last one stopped\n");

  join (images, sizeof images, g_root, "images");
  set_env ("CUSTOM_SCANNER_MODE", "adf");
  set_env ("CUSTOM_SCANNER_MAXCOUNT", "2");
  reset_state ("state-adf");
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");

  for (i = 0; i < 3; i++)
  {
    char what[64];
    int  page;

    snprintf (what, sizeof what, "job %d", i + 1);
    scan_job (handle, &job, MAX_PAGES);
    expected[0] = (i * 2) % IMG_COUNT;
    expected[1] = (i * 2 + 1) % IMG_COUNT;
    check_pages (&job, what, 2, expected);
    for (page = 0; page < 2; page++)
      check_rgb_content (&job, what, page, expected[page], 1);
  }

  sane_custom_scanner_close (handle);
}

static void test_source_option (void)
{
  char             images[512];
  SANE_Handle      handle = NULL;
  Job              job;
  int              expected[IMG_COUNT];
  SANE_Int         value;
  char             source[64];
  const SANE_Option_Descriptor *descriptor;
  int              info = 0;
  int              i;

  printf ("source option: flatbed scans one image, the document feeder scans the whole set\n");

  join (images, sizeof images, g_root, "images");
  set_env ("CUSTOM_SCANNER_MODE", "auto");
  set_env ("CUSTOM_SCANNER_MAXCOUNT", NULL);
  reset_state ("state-source");
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");

  descriptor = sane_custom_scanner_get_option_descriptor (handle, 0);
  CHECK (descriptor != NULL, "option 0 is missing");
  if (descriptor)
  {
    CHECK (sane_custom_scanner_control_option (handle, 0, SANE_ACTION_GET_VALUE, &value, &info) == SANE_STATUS_GOOD,
           "cannot read the number of options");
    CHECK (value == 3, "expected 3 options (num_options, source, mode), got %d", value);
  }

  descriptor = sane_custom_scanner_get_option_descriptor (handle, 1);
  CHECK (descriptor != NULL, "the source option is missing");
  if (descriptor)
  {
    CHECK (descriptor->type == SANE_TYPE_STRING, "the source option is not a string");
    CHECK (descriptor->constraint_type == SANE_CONSTRAINT_STRING_LIST, "the source option has no string list");
  }

  memset (source, 0, sizeof source);
  CHECK (sane_custom_scanner_control_option (handle, 1, SANE_ACTION_GET_VALUE, source, &info) == SANE_STATUS_GOOD,
         "cannot read the source option");
  CHECK (strcmp (source, "Flatbed") == 0, "the default source is '%s', expected 'Flatbed'", source);

  /* Flatbed: a single image, the cycle position advances by one. */
  expected[0] = 0;
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "flatbed job 1", 1, expected);
  check_rgb_content (&job, "flatbed job 1", 0, 0, 1);

  expected[0] = 1;
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "flatbed job 2", 1, expected);

  /* Document feeder: every image, starting at the current cycle position. */
  CHECK (sane_custom_scanner_control_option (handle, 1, SANE_ACTION_SET_VALUE,
                                             (void *) "Automatic Document Feeder",
                                             &info) == SANE_STATUS_GOOD,
         "cannot select the document feeder");
  for (i = 0; i < IMG_COUNT; i++)
    expected[i] = (2 + i) % IMG_COUNT;
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "adf job", IMG_COUNT, expected);

  CHECK (sane_custom_scanner_control_option (handle, 1, SANE_ACTION_SET_VALUE, (void *) "Flatbed", &info) ==
             SANE_STATUS_GOOD,
         "cannot select the flatbed");
  expected[0] = (2 + IMG_COUNT) % IMG_COUNT;
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "flatbed job 3", 1, expected);

  /* Unknown sources must be rejected. */
  CHECK (sane_custom_scanner_control_option (handle, 1, SANE_ACTION_SET_VALUE, (void *) "Nope", &info) !=
             SANE_STATUS_GOOD,
         "an unknown source was accepted");

  sane_custom_scanner_close (handle);
}

static void test_single_and_index (void)
{
  SANE_Handle handle = NULL;
  Job         job;
  int         expected[1];
  int         i;

  printf ("single mode / fixed index: the same image over and over again\n");

  set_env ("CUSTOM_SCANNER_MODE", "single");
  set_env ("CUSTOM_SCANNER_INDEX", NULL);
  reset_state ("state-single");
  reinit ();
  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  for (i = 0; i < 3; i++)
  {
    expected[0] = 0;
    scan_job (handle, &job, MAX_PAGES);
    check_pages (&job, "single job", 1, expected);
  }
  sane_custom_scanner_close (handle);

  set_env ("CUSTOM_SCANNER_INDEX", "2");
  reset_state ("state-index");
  reinit ();
  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  for (i = 0; i < 3; i++)
  {
    expected[0] = 2;
    scan_job (handle, &job, MAX_PAGES);
    check_pages (&job, "job with index=2", 1, expected);
  }
  sane_custom_scanner_close (handle);
}

static void test_formats (void)
{
  char        dir[512];
  char        path[600];
  SANE_Handle handle = NULL;
  Job         job;
  int         expected[1];
  int         i;

  printf ("formats: gray (P5), 16 bit (maxval 65535) and images with header comments\n");

  join (dir, sizeof dir, g_root, "formats");
  make_dir (dir);

  join (path, sizeof path, dir, "1-gray.pnm");
  write_image (path, 0, 7, 255, 0);
  join (path, sizeof path, dir, "2-deep.pnm");
  write_image (path, 1, 5, 65535, 1);

  set_env ("CUSTOM_SCANNER_IMAGE_DIR", dir);
  set_env ("CUSTOM_SCANNER_MODE", "cycle");
  set_env ("CUSTOM_SCANNER_CHANNEL_ORDER", "rgb");
  set_env ("CUSTOM_SCANNER_INDEX", NULL);
  reset_state ("state-formats");
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");

  /* 1-gray.pnm: a gray source scanned in the default Color mode is
     expanded to rgb (every sample of image id 7 is 7). */
  scan_job (handle, &job, MAX_PAGES);
  expected[0] = 7;
  check_pages (&job, "gray image", 1, expected);
  if (!job.error && job.pages == 1)
  {
    CHECK (job.format[0] == SANE_FRAME_RGB, "the gray image was not expanded to rgb");
    CHECK (job.depth[0] == 8, "the gray image has depth %d", job.depth[0]);
    CHECK (job.bytes_per_line[0] == IMG_W * 3, "the gray image has bytes_per_line %d", job.bytes_per_line[0]);
    CHECK (job.bytes[0] == IMG_W * IMG_H * 3, "the gray image has %ld bytes", job.bytes[0]);
    for (i = 0; i < (int) job.bytes[0]; i++)
      CHECK (job.data[0][i] == 7, "gray image byte %d is %d, expected 7", i, job.data[0][i]);
  }

  /* 2-deep.pnm: 16 bit source, down sized to 8 bit per sample. */
  scan_job (handle, &job, MAX_PAGES);
  expected[0] = 5;
  check_pages (&job, "16 bit image", 1, expected);
  check_rgb_content (&job, "16 bit image", 0, 5, 1);

  /* Third job (cycle wraps to the gray image) in Gray mode: the gray
     source passes through unchanged. */
  {
    int info = 0;

    CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Gray", &info) ==
               SANE_STATUS_GOOD,
           "cannot select Gray in the formats test");
    scan_job (handle, &job, MAX_PAGES);
    check_pages (&job, "gray image, gray mode", 1, NULL);
    if (!job.error && job.pages == 1)
    {
      CHECK (job.format[0] == SANE_FRAME_GRAY, "gray mode did not hand out a gray frame");
      CHECK (job.bytes_per_line[0] == IMG_W, "gray mode bytes_per_line %d", job.bytes_per_line[0]);
      CHECK (job.bytes[0] == IMG_W * IMG_H, "gray mode has %ld bytes", job.bytes[0]);
      for (i = 0; i < (int) job.bytes[0]; i++)
        CHECK (job.data[0][i] == 7, "gray mode byte %d is %d, expected 7", i, job.data[0][i]);
    }
  }

  sane_custom_scanner_close (handle);
}

static void test_channel_order (void)
{
  char        images[512];
  SANE_Handle handle = NULL;
  Job         job;

  printf ("channel_order: rgb is the default, gbr (legacy) rotates the channels\n");

  join (images, sizeof images, g_root, "images");
  set_env ("CUSTOM_SCANNER_IMAGE_DIR", images);
  set_env ("CUSTOM_SCANNER_CHANNEL_ORDER", NULL); /* default: rgb */
  set_env ("CUSTOM_SCANNER_MODE", "cycle");
  set_env ("CUSTOM_SCANNER_INDEX", "0");
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "default job", 1, NULL);
  check_rgb_content (&job, "default job", 0, 0, 1);
  sane_custom_scanner_close (handle);

  set_env ("CUSTOM_SCANNER_CHANNEL_ORDER", "gbr");
  reinit ();
  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "legacy job", 1, NULL); /* the first byte is the green channel */
  check_rgb_content (&job, "legacy job", 0, 0, 0);

  sane_custom_scanner_close (handle);
}

static void test_modes (void)
{
  char             images[512];
  SANE_Handle      handle = NULL;
  Job              job;
  const SANE_Option_Descriptor *descriptor;
  int              info = 0;
  int              i;

  printf ("mode option: Color, Gray, Binary/Lineart convert the scanned page\n");

  join (images, sizeof images, g_root, "images");
  set_env ("CUSTOM_SCANNER_IMAGE_DIR", images);
  set_env ("CUSTOM_SCANNER_CHANNEL_ORDER", NULL);
  set_env ("CUSTOM_SCANNER_MODE", "single");
  set_env ("CUSTOM_SCANNER_INDEX", "3"); /* image id 3: pixels (3, 4, 5) */
  reinit ();

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");

  descriptor = sane_custom_scanner_get_option_descriptor (handle, 2);
  CHECK (descriptor != NULL, "the mode option is missing");
  if (descriptor)
  {
    CHECK (strcmp (descriptor->name, SANE_NAME_SCAN_MODE) == 0, "option 2 is not '%s'", SANE_NAME_SCAN_MODE);
    CHECK (descriptor->type == SANE_TYPE_STRING, "the mode option is not a string");
    CHECK (descriptor->constraint_type == SANE_CONSTRAINT_STRING_LIST, "the mode option has no string list");
  }

  /* Default mode is Color. */
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "color job", 1, NULL);
  check_rgb_content (&job, "color job", 0, 3, 1);

  /* Gray: BT.601 luminance of (3, 4, 5) = (3*299 + 4*587 + 5*114) / 1000 = 3. */
  CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Gray", &info) ==
             SANE_STATUS_GOOD,
         "cannot select Gray");
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "gray job", 1, NULL);
  if (!job.error && job.pages == 1)
  {
    CHECK (job.format[0] == SANE_FRAME_GRAY, "the gray job is not gray");
    CHECK (job.depth[0] == 8, "the gray job has depth %d", job.depth[0]);
    CHECK (job.bytes_per_line[0] == IMG_W, "the gray job has bytes_per_line %d", job.bytes_per_line[0]);
    CHECK (job.bytes[0] == IMG_W * IMG_H, "the gray job has %ld bytes", job.bytes[0]);
    for (i = 0; i < (int) job.bytes[0]; i++)
      CHECK (job.data[0][i] == 3, "gray job byte %d is %d, expected 3", i, job.data[0][i]);
  }

  /* Binary: every sample of image 3 is dark, so all bits are 0. */
  CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Binary", &info) ==
             SANE_STATUS_GOOD,
         "cannot select Binary");
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "binary job", 1, NULL);
  if (!job.error && job.pages == 1)
  {
    CHECK (job.format[0] == SANE_FRAME_GRAY, "the binary job is not gray-framed");
    CHECK (job.depth[0] == 1, "the binary job has depth %d, expected 1", job.depth[0]);
    CHECK (job.bytes_per_line[0] == (IMG_W + 7) / 8, "the binary job has bytes_per_line %d",
           job.bytes_per_line[0]);
    CHECK (job.bytes[0] == (size_t) ((IMG_W + 7) / 8) * IMG_H, "the binary job has %ld bytes", job.bytes[0]);
    for (i = 0; i < (int) job.bytes[0]; i++)
      CHECK (job.data[0][i] == 0, "binary job byte %d is %d, expected 0", i, job.data[0][i]);
  }

  /* Lineart is an alias for Binary; image 0 is bright enough for set bits. */
  set_env ("CUSTOM_SCANNER_INDEX", "0"); /* image id 0: pixels (0, 1, 2), luminance 1 */
  sane_custom_scanner_close (handle);
  reinit ();
  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Lineart", &info) ==
             SANE_STATUS_GOOD,
         "cannot select Lineart");
  scan_job (handle, &job, MAX_PAGES);
  check_pages (&job, "lineart job", 1, NULL);
  if (!job.error && job.pages == 1)
  {
    CHECK (job.depth[0] == 1, "the lineart job has depth %d", job.depth[0]);
    /* Luminance of (0, 1, 2) = (0 + 587 + 228) / 1000 = 0 -> all bits 0. */
    for (i = 0; i < (int) job.bytes[0]; i++)
      CHECK (job.data[0][i] == 0, "lineart job byte %d is %d, expected 0", i, job.data[0][i]);
  }

  /* A gray (P5) source requested as Color is expanded to rgb. */
  sane_custom_scanner_close (handle);
  {
    char dir[512];
    char path[600];

    join (dir, sizeof dir, g_root, "gray-src");
    make_dir (dir);
    join (path, sizeof path, dir, "1-gray.pnm");
    write_image (path, 0, 200, 255, 0);

    set_env ("CUSTOM_SCANNER_IMAGE_DIR", dir);
    set_env ("CUSTOM_SCANNER_INDEX", NULL);
    reinit ();
    CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
    CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Color", &info) ==
               SANE_STATUS_GOOD,
           "cannot select Color");
    scan_job (handle, &job, MAX_PAGES);
    check_pages (&job, "gray source, color job", 1, NULL);
    if (!job.error && job.pages == 1)
    {
      CHECK (job.format[0] == SANE_FRAME_RGB, "the expanded job is not rgb");
      CHECK (job.bytes[0] == PAGE_BYTES, "the expanded job has %ld bytes", job.bytes[0]);
      for (i = 0; i < (int) job.bytes[0]; i++)
        CHECK (job.data[0][i] == 200, "expanded job byte %d is %d, expected 200", i, job.data[0][i]);
    }

    /* Binary on the bright gray image: every pixel is set, so each line
       packs its 4 pixels into the high nibble (0xf0). */
    CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Binary", &info) ==
               SANE_STATUS_GOOD,
           "cannot select Binary");
    scan_job (handle, &job, MAX_PAGES);
    check_pages (&job, "bright binary job", 1, NULL);
    if (!job.error && job.pages == 1)
    {
      CHECK (job.depth[0] == 1, "the bright binary job has depth %d", job.depth[0]);
      CHECK (job.bytes[0] == (size_t) ((IMG_W + 7) / 8) * IMG_H, "the bright binary job has %ld bytes",
             job.bytes[0]);
      for (i = 0; i < (int) job.bytes[0]; i++)
        CHECK (job.data[0][i] == 0xf0, "bright binary byte %d is 0x%02x, expected 0xf0", i, job.data[0][i]);
    }
  }

  /* Unknown modes are rejected. */
  CHECK (sane_custom_scanner_control_option (handle, 2, SANE_ACTION_SET_VALUE, (void *) "Nope", &info) !=
             SANE_STATUS_GOOD,
         "an unknown mode was accepted");

  sane_custom_scanner_close (handle);
}

static void test_errors (void)
{
  char        empty[512];
  SANE_Handle handle = NULL;
  Job         job;
  const SANE_Device **devices = NULL;

  printf ("error handling: empty image directory and device list\n");

  join (empty, sizeof empty, g_root, "empty");
  make_dir (empty);

  set_env ("CUSTOM_SCANNER_IMAGE_DIR", empty);
  set_env ("CUSTOM_SCANNER_IMAGE", NULL);
  set_env ("CUSTOM_SCANNER_INDEX", NULL);
  set_env ("CUSTOM_SCANNER_MODE", "auto");
  reset_state ("state-empty");
  reinit ();

  CHECK (sane_custom_scanner_get_devices (&devices, SANE_TRUE) == SANE_STATUS_GOOD, "sane_get_devices failed");
  CHECK (devices != NULL && devices[0] != NULL, "no device was reported");
  if (devices && devices[0])
  {
    /* The backend reports the device without the backend prefix; the SANE dll
       layer turns "virtual" into "custom_scanner:virtual". */
    CHECK (strcmp (devices[0]->name, "virtual") == 0,
           "the device name is '%s', expected 'virtual'", devices[0]->name);
    CHECK (strchr (devices[0]->name, ':') == NULL,
           "the device name '%s' must not contain a ':' (the dll layer adds the prefix)", devices[0]->name);
    CHECK (devices[1] == NULL, "the device list is not NULL terminated");
  }

  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  scan_job (handle, &job, MAX_PAGES);
  CHECK (!job.error, "an empty image set must not fail, but: %s", job.error_text ? job.error_text : "");
  CHECK (job.pages == 0, "an empty image set returned %d page(s)", job.pages);

  sane_custom_scanner_close (handle);

  /* A configured but missing image file must be handled as well. */
  set_env ("CUSTOM_SCANNER_IMAGE", "/nonexistent/image.pnm");
  reinit ();
  CHECK (sane_custom_scanner_open ("custom_scanner:virtual", &handle) == SANE_STATUS_GOOD, "sane_open failed");
  scan_job (handle, &job, MAX_PAGES);
  CHECK (job.pages == 0, "a missing image file returned %d page(s)", job.pages);
  sane_custom_scanner_close (handle);

  set_env ("CUSTOM_SCANNER_IMAGE", NULL);
}

/* ------------------------------------------------------------------------- */

int main (void)
{
  char images[512];
  char template_dir[512];
  char command[512];

  snprintf (template_dir, sizeof template_dir, "/tmp/custom_scanner_test.XXXXXX");
  {
    char *created = mkdtemp (template_dir);

    if (!created)
    {
      perror ("mkdtemp");
      return 2;
    }
    snprintf (g_root, sizeof g_root, "%s", created);
  }

  join (images, sizeof images, g_root, "images");
  make_dir (images);
  write_image_set (images);

  test_cycle ();
  test_adf_maxcount ();
  test_source_option ();
  test_single_and_index ();
  test_formats ();
  test_channel_order ();
  test_modes ();
  test_errors ();

  snprintf (command, sizeof command, "rm -rf '%s'", g_root);
  if (system (command) != 0)
    printf ("note: could not remove %s\n", g_root);

  printf ("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  if (g_failures)
  {
    printf ("SELFTEST FAILED\n");
    return 1;
  }
  printf ("SELFTEST PASSED\n");
  return 0;
}
