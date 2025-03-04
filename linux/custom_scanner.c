#include <sane/sane.h>
#include <sane/saneopts.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CUSTOM_IMAGE_PATH "/home/xiao/Desktop/dwt/test.pnm"
#define OPTION_NUM 1

static SANE_Device mock_device = {
    .name = "my-scanner",
    .vendor = "Custom",
    .model = "Virtual Scanner",
    .type = "Test"};

static const SANE_Option_Descriptor options[] = {
    {.name = SANE_TITLE_NUM_OPTIONS,
     .title = "Number of options",
     .type = SANE_TYPE_INT,
     .size = sizeof(SANE_Int),
     .cap = SANE_CAP_SOFT_DETECT,
     .constraint_type = SANE_CONSTRAINT_NONE}};

SANE_Status sane_custom_scanner_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
{
    fprintf(stderr, "sane_custom_scanner_init...\n");
    if (version_code)
        *version_code = SANE_VERSION_CODE(1, 0, 0);
    return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_get_devices(const SANE_Device ***devices, SANE_Bool local_only)
{
    static const SANE_Device *devs[] = {&mock_device, NULL};
    *devices = devs;
    return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_open(SANE_String_Const name, SANE_Handle *handle)
{
    fprintf(stderr, "sane_custom_scanner_open...\n");
    *handle = (SANE_Handle)&mock_device;
    return SANE_STATUS_GOOD;
}

void sane_custom_scanner_close(SANE_Handle handle)
{
    fprintf(stderr, "sane_custom_scanner_close...\n");
}

SANE_Status sane_custom_scanner_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action, void *value, SANE_Int *info)
{
    fprintf(stderr, "sane_custom_scanner_control_option...\n");
    if (option == 0 && action == SANE_ACTION_GET_VALUE)
    {
        *(SANE_Int *)value = OPTION_NUM;
        return SANE_STATUS_GOOD;
    }
    return SANE_STATUS_UNSUPPORTED;
}

const SANE_Option_Descriptor *sane_custom_scanner_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
    fprintf(stderr, "sane_custom_scanner_get_option_descriptor...\n");
    if (option == 0)
        return &options[0];
    return NULL;
}

SANE_Status sane_custom_scanner_get_parameters(SANE_Handle handle, SANE_Parameters *params)
{
    fprintf(stderr, "sane_custom_scanner_get_parameters...\n");

    if (params)
    {
        FILE *fp = fopen(CUSTOM_IMAGE_PATH, "rb");
        if (fp)
        {
            char magic[3];
            int width, height, maxval;

            fscanf(fp, "%2s", magic);

            char ch;
            while ((ch = fgetc(fp)) == '#')
            {
                while (fgetc(fp) != '\n')
                    ;
            }
            ungetc(ch, fp);

            fscanf(fp, "%d %d %d", &width, &height, &maxval);
            fclose(fp);

            params->pixels_per_line = width;
            params->lines = height;

            if (strcmp(magic, "P6") == 0)
            {
                params->format = SANE_FRAME_RGB;
                params->depth = 8;
                params->bytes_per_line = width * 3;
            }
            else
            {
                params->format = SANE_FRAME_GRAY;
                params->depth = 8;
                params->bytes_per_line = width;
            }
        }
    }
    else
    {
        params->format = SANE_FRAME_RED;
        params->depth = 1;
        return SANE_STATUS_EOF;
    }
    return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_start(SANE_Handle handle)
{
    fprintf(stderr, "sane_custom_scanner_start...\n");
    static int start_count = 0;

    if (start_count == 1)
        return SANE_STATUS_EOF;

    start_count++;
    return SANE_STATUS_GOOD;
}

SANE_Status sane_custom_scanner_read(SANE_Handle handle, SANE_Byte *data, SANE_Int max_length, SANE_Int *length)
{
    fprintf(stderr, "sane_custom_scanner_read...\n");
    static FILE *fp = NULL;
    static long pixel_data_offset = 0;
    static long total_pixels_bytes = 0;
    static int width = 0, height = 0;
    static int bytes_per_line = 0;
    static int header_sent = 0;
    static long total_read = 0;

    if (!fp)
    {

        fp = fopen(CUSTOM_IMAGE_PATH, "rb");
        if (!fp)
            return SANE_STATUS_IO_ERROR;

        char magic[3];
        int width, height, max_val;
        fscanf(fp, "%2s", magic);
        fscanf(fp, "%d %d", &width, &height);
        fscanf(fp, "%d", &max_val);
        fgetc(fp);
        pixel_data_offset = ftell(fp);

        fseek(fp, 0, SEEK_END);
        total_read = ftell(fp);
        rewind(fp);

        size_t actual_read = fread(data, 1, pixel_data_offset, fp);

        total_read -= actual_read;

        *length = actual_read;

        return SANE_STATUS_GOOD;
    }

    if (total_read <= 0)
    {
        *length = 0;
        fclose(fp);
        fp = NULL;
        return SANE_STATUS_EOF;
    }

    size_t rgb_block_size = (max_length / 3) * 3;
    size_t read_size = fmin(rgb_block_size, total_read);
    size_t actual_read = fread(data, 1, read_size, fp);
    total_read -= actual_read;

    *length = actual_read;

    for (int i = 0; i < *length; i += 3)
    {
        SANE_Byte temp = data[i + 1];
        data[i + 1] = data[i + 2];
        data[i + 2] = data[i];
        data[i] = temp;
    }

    return SANE_STATUS_GOOD;
}

void sane_custom_scanner_cancel(SANE_Handle handle) { fprintf(stderr, "sane_custom_scanner_cancel...\n"); }
void sane_custom_scanner_exit() { fprintf(stderr, "sane_custom_scanner_exit...\n"); }
// SANE_Status sane_custom_scanner_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
// {
//     fprintf(stderr, "sane_custom_scanner_set_io_mode...\n");
//     return SANE_STATUS_UNSUPPORTED;
// }
// SANE_Status sane_custom_scanner_get_select_fd(SANE_Handle handle, SANE_Int *fd)
// {
//     fprintf(stderr, "sane_custom_scanner_get_select_fd...\n");
//     return SANE_STATUS_UNSUPPORTED;
// }
