#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define ASSERT(EXPR) if (!(EXPR)) { printf ("Assert failed [%s():%d]: %s\n", __func__, __LINE__, #EXPR); *(volatile int *) 0 = 0; }
#define BIN_FMT "%d%d%d%d %d%d%d%d"
#define BIN_VAL(BYTE) \
    (BYTE & (1 << 7) ? 1 : 0), \
    (BYTE & (1 << 6) ? 1 : 0), \
    (BYTE & (1 << 5) ? 1 : 0), \
    (BYTE & (1 << 4) ? 1 : 0), \
    (BYTE & (1 << 3) ? 1 : 0), \
    (BYTE & (1 << 2) ? 1 : 0), \
    (BYTE & (1 << 1) ? 1 : 0), \
    (BYTE & (1 << 0) ? 1 : 0)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef union
{
    struct
    {
        char *byte;
        char *word;
    };
    char *array[2];
} op;

static op ops[] = {
    [0b000] = { "al", "ax" },
    [0b001] = { "cl", "cx" },
    [0b010] = { "dl", "dx" },
    [0b011] = { "bl", "bx" },
    [0b100] = { "ah", "sp" },
    [0b101] = { "ch", "bp" },
    [0b110] = { "dh", "si" },
    [0b111] = { "bh", "di" },
};

static char *
binary_print (char *str, size_t len, u8 byte)
{
    char *ptr = str;
    bool wrote_one = false;
    int last_one = -1;

    memset (str, 0, len);

    snprintf (ptr, len, "0b");
    ptr += 2;
    len -= 2;

    for (int i = 8; i >= 0; i--)
    {
        if ((byte & (1 << i)) != 0)
        {
            snprintf (ptr, len, "1");
            ptr++;
            len--;

            last_one = i;
        }
        else if (last_one > -1)
        {
            snprintf (ptr, len, "0");
            ptr++;
            len--;
        }
    }

    if (last_one == -1)
    {
        snprintf (ptr, len, "0");
    }

    return str;
}

enum mov_type
{
    MOV__INVALID = 0,

    MOV__REGISTER_TO_REGISTER,
    MOV__IMMEDIATE_TO_MEMORY,
    MOV__IMMEDIATE_TO_REGISTER,
    MOV__MEMORY_TO_ACCUMULATOR,
    MOV__ACCUMULATOR_TO_MEMORY,
    MOV__RM_TO_SEG,
    MOV__SEG_TO_RM,
};

static enum mov_type
mov_type_get (u8 byte)
{
    enum mov_type type = MOV__INVALID;

    if (byte >> 2 == 0b100010)
    {
        type = MOV__REGISTER_TO_REGISTER;
    }
    else if (byte >> 1 == 0b1100011)
    {
        type = MOV__IMMEDIATE_TO_MEMORY;
    }
    else if (byte >> 4 == 0b1011)
    {
        type = MOV__IMMEDIATE_TO_REGISTER;
    }
    else if (byte >> 1 == 0b1010000)
    {
        type = MOV__MEMORY_TO_ACCUMULATOR;
    }
    else if (byte >> 1 == 0b1010001)
    {
        type = MOV__ACCUMULATOR_TO_MEMORY;
    }
    else if (byte == 0b10001110)
    {
        type = MOV__RM_TO_SEG;
    }
    else if (byte == 0b10001100)
    {
        type = MOV__SEG_TO_RM;
    }
    else
    {
        printf ("Unknown mov type [" BIN_FMT "]\n", BIN_VAL (byte));
    }

    return type;
}

static u32
decode_mov (FILE *fp, u8 *data, enum mov_type type)
{
    u32 i = 0;
    u32 bytes_consumed;

    switch (type)
    {
        case MOV__REGISTER_TO_REGISTER:
        {
            u8 b0 = data[i++];
            u8 b1 = data[i++];

            u8 D = b0 & 0b10; // 0010
            u8 W = b0 & 0b01; // 0001

            printf (" mov (register-to-register)\n");
            printf ("  D: %u\n", D);
            printf ("  W: %u\n", W);

            u8 mod = (b1 >> 6) & 0b11;  // 1100 0000
            u8 reg = (b1 >> 3) & 0b111; // 0011 1000
            u8 rm = b1 & 0b111;         // 0000 0111

            char str[32];
            printf (" mod: %s\n", binary_print (str, sizeof (str), mod));
            printf (" reg: %s\n", binary_print (str, sizeof (str), reg));
            printf (" r/m: %s\n", binary_print (str, sizeof (str), rm));

            char *dest;
            char *source;

            if (D == 1)
            {
                dest = ops[reg].array[W];
                source = ops[rm].array[W];
            }
            else
            {
                dest = ops[rm].array[W];
                source = ops[reg].array[W];
            }

            printf (" ----------\n");
            printf (" mov %s, %s\n", dest, source);
            printf (" ----------\n");
            fprintf (fp, "mov %s, %s\n", dest, source);

            bytes_consumed = i;
        } break;
        case MOV__IMMEDIATE_TO_REGISTER:
        {
            u8 b0 = data[i++];
            u8 data_byte = data[i++];
            u8 data_word = data[i++];

            u8 low = b0 << 4;
            u8 W = low & 0b1000;
            u8 reg = low & 0b0111;
            char str[128] = {0};

            printf (" mov (immediate-to-register)\n");
            printf ("  low : " BIN_FMT "\n", BIN_VAL (low));
            printf ("  W   : %u\n", (W >> 1));
            printf ("  reg : %s\n", binary_print (str, sizeof (str), reg));

            bytes_consumed = i;
        } break;
        default:
        {
            bytes_consumed = 2;
        } break;
    }

    return bytes_consumed;
}

static void
decode (u8 *data, int len)
{
    FILE *fp = fopen ("test.asm", "wb");
    fprintf (fp, "; disassembly\n\n");
    fprintf (fp, "bits 16\n\n");

    u32 bytes_consumed = 0;
    for (u32 i = 0; i < len; i += bytes_consumed)
    {
        u8 byte = data[i];
        enum mov_type type = mov_type_get (byte);
        if (type != MOV__INVALID)
        {
            bytes_consumed = decode_mov (fp, &data[i], type);
        }

#if 0
        u8 b0 = data[i];
        u8 b1 = data[i + 1];

        printf ("b0: 0x%02X (" BIN_FMT ")\n", b0, BIN_VAL (b0));
        printf ("b1: 0x%02X (" BIN_FMT ")\n", b1, BIN_VAL (b1));

        if (b0 >> 2 == 0b100010) // mov - register-to-register
        {
            u8 D = b0 & 0b10; // 0010
            u8 W = b0 & 0b01; // 0001

            printf (" mov (register-to-register)\n");
            printf ("  D: %u\n", D);
            printf ("  W: %u\n", W);

            u8 mod = (b1 >> 6) & 0b11;  // 1100 0000
            u8 reg = (b1 >> 3) & 0b111; // 0011 1000
            u8 rm = b1 & 0b111;         // 0000 0111

            char str[32];
            printf (" mod: %s\n", binary_print (str, sizeof (str), mod));
            printf (" reg: %s\n", binary_print (str, sizeof (str), reg));
            printf (" r/m: %s\n", binary_print (str, sizeof (str), rm));

            char *dest;
            char *source;

            if (D == 1)
            {
                dest = ops[reg].array[W];
                source = ops[rm].array[W];
            }
            else
            {
                dest = ops[rm].array[W];
                source = ops[reg].array[W];
            }

            printf (" ----------\n");
            printf (" mov %s, %s\n", dest, source);
            printf (" ----------\n");
            fprintf (fp, "mov %s, %s\n", dest, source);
        }
        else if (b0 >> 4 == 0b1011) // immediate-to-register
        {
            u8 low = b0 << 4;
            u8 W = low & 0b1000;
            u8 reg = low & 0b0111;
            char str[128] = {0};

            printf (" mov (immediate-to-register)\n");
            printf ("  low : " BIN_FMT "\n", BIN_VAL (low));
            printf ("  W   : %u\n", (W >> 1));
            printf ("  reg : %s\n", binary_print (str, sizeof (str), reg));
        }
        else
        {
            printf (" Unknown instruction 0x%02X (" BIN_FMT ")\n", b0, BIN_VAL (b0));
        }
#endif
    }

    fclose (fp);
}

static u8 *
read_file (char *file, int *read_len)
{
    int len = 0;
    u8 *data = NULL;
    bool ok = false;

    FILE *fp = fopen (file, "rb");
    if (fp)
    {
        fseek (fp, 0, SEEK_END);
        len = ftell (fp);
        fseek (fp, 0, SEEK_SET);

        data = (u8 *) malloc (len);
        memset (data, 0, len);
        fread (data, len, 1, fp);

        *read_len = len;

        fclose (fp);
    }

    ok = (data && len > 0);
    ASSERT (ok);

    printf ("Read [%s %d bytes] %s\n", file, len, (ok ? "OK" : "Error"));

#if 1
    int n_bytes = 0;
    for (int i = 0; i < len; i++)
    {
        u8 byte = data[i];

        printf ("  0x%02X (" BIN_FMT ") ", byte, BIN_VAL (byte));

        if (++n_bytes == 2)
        {
            printf ("\n");
            n_bytes = 0;
        }
    }
    printf ("\n");
#endif

    return data;
}

int
main (int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        int len = 0;
        u8 *data = NULL;

        data = read_file (argv[i], &len);
        if (data && len > 0)
        {
            decode (data, len);
            free (data);
        }
    }

    return 0;
}
