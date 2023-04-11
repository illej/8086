#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define ASSERT(EXPR) if (!(EXPR)) { fprintf (stderr, "Assert failed [%s():%d]: if (%s) ..\n", __func__, __LINE__, #EXPR); *(volatile int *) 0 = 0; }
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
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

typedef union
{
    struct
    {
        char *byte;
        char *word;
    };
    char *array[2];
} reg;

enum instruction_type
{
    INVALID = 0,

    MOV__REGISTER_TO_REGISTER,
    MOV__IMMEDIATE_TO_MEMORY,
    MOV__IMMEDIATE_TO_REGISTER,
    MOV__MEMORY_TO_ACCUMULATOR,
    MOV__ACCUMULATOR_TO_MEMORY,
    MOV__RM_TO_SEG,
    MOV__SEG_TO_RM,

    ADD__REG_TO_REG,
    ADD__IMMEDIATE_TO_RM,
    ADD__IMMEDIATE_TO_ACCUMULATOR,

    JMP,
};

struct instruction
{
    enum instruction_type type;

    /* width
     *
     * 0: 8-bit (byte)
     * 1: 16-bit (word)
     */
    u8 w;

    /* direction
     *
     * 0: REG is source
     * 1: REG is destination
     */
    u8 d;

     /* signed bit extension
     *
     * 0: no sign extension
     * 1: sign extend 8-bit immediate data to 16-bits if w=1
     */
    u8 s;

    /* shift/rotate
     *
     * 0: shift/rotate count is 1
     * 1: shift/rotate count in CL register
     */
    u8 v;

    /* repeat/loop
     *
     * 0: repeat/loop while zero flag is clear
     * 1: repeat/loop while zero flag is set
     */
    u8 z;

    u8 mod;
    u8 reg;
    u8 rm;

    char *eac;

    u16 disp;
    u16 data;
};

static FILE *fp;
static reg registers[] = {
    [0b000] = { "al", "ax" },
    [0b001] = { "cl", "cx" },
    [0b010] = { "dl", "dx" },
    [0b011] = { "bl", "bx" },
    [0b100] = { "ah", "sp" },
    [0b101] = { "ch", "bp" },
    [0b110] = { "dh", "si" },
    [0b111] = { "bh", "di" },
};
static char *eac_table[] = {
    [0b000] = "bx + si",
    [0b001] = "bx + di",
    [0b010] = "bp + si",
    [0b011] = "bp + di",
    [0b100] = "si",
    [0b101] = "di",
    [0b110] = "bp",
    [0b111] = "bx",
};

static char *
type_str (enum instruction_type type)
{
    switch (type)
    {
        case INVALID:
        {
            return "Invalid";
        } break;
        case MOV__REGISTER_TO_REGISTER:
        case MOV__IMMEDIATE_TO_MEMORY:
        case MOV__IMMEDIATE_TO_REGISTER:
        case MOV__MEMORY_TO_ACCUMULATOR:
        case MOV__ACCUMULATOR_TO_MEMORY:
        case MOV__RM_TO_SEG:
        case MOV__SEG_TO_RM:
        {
            return "mov";
        } break;
        case ADD__REG_TO_REG:
        case ADD__IMMEDIATE_TO_RM:
        case ADD__IMMEDIATE_TO_ACCUMULATOR:
        {
            return "add";
        } break;
        case JMP:
        {
            return "jmp";
        } break;
    }

    return "Unknown";
}

static enum instruction_type
instruction_type_get (u8 byte)
{
    enum instruction_type type = INVALID;

    if (byte >> 2 == 0b000000)
    {
        type = ADD__REG_TO_REG;
    }
    else if (byte >> 2 == 0b100000)
    {
        type = ADD__IMMEDIATE_TO_RM;
    }
    else if (byte >> 1 == 0b0000010)
    {
        type = ADD__IMMEDIATE_TO_ACCUMULATOR;
    }
    else if (byte >> 2 == 0b100010)
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

    return type;
}

static char *
binary_print (char *str, size_t len, u8 byte)
{
    char *ptr = str;
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

static u32
decode_mov (u8 *data, struct instruction *inst)
{
    u32 i = 0;
    u32 bytes_consumed = 0;

    switch (inst->type)
    {
        case MOV__REGISTER_TO_REGISTER:
        {
            u8 b0 = data[i++];
            u8 b1 = data[i++];

            u8 D = (b0 & 0b10) != 0; // 0000 0010
            u8 W = (b0 & 0b01) != 0; // 0000 0001

            u8 mod = (b1 >> 6) & 0b11;  // 1100 0000
            u8 reg = (b1 >> 3) & 0b111; // 0011 1000
            u8 rm = b1 & 0b111;         // 0000 0111

            switch (mod)
            {
                case 0b00:
                {
                    /**
                     * Memory mode, no displacements follows
                     * (Except when R/M = 110, then 16-bit
                     * displacement follows)
                     */
                    if (rm == 0b110)
                    {
                        u8 b2 = data[i++];
                        u8 b3 = data[i++];
                        inst->disp = (b3 << 8) | b2;
                    }
                } break;
                case 0b01:
                {
                    /**
                     * Memory mode, 8-bit displacement follows
                     */
                    inst->disp = data[i++];
                } break;
                case 0b10:
                {
                    /**
                     * Memory mode, 16-bit displacement follows
                     */
                    u8 disp_low = data[i++];
                    u8 disp_high = data[i++];
                    inst->disp = (disp_high << 8) | disp_low;
                } break;
                case 0b11:
                {
                    /**
                     * Register mode (no displacement)
                     */
                } break;
                default:
                {
                    ASSERT (!"Invalid mod value");
                } break;
            }

            inst->w = W;
            inst->d = D;
            inst->mod = mod;
            inst->reg = reg;
            inst->rm = rm;

            bytes_consumed = i;
        } break;
        case MOV__IMMEDIATE_TO_REGISTER:
        {
            u8 b0 = data[i++];

            inst->w = (b0 & 0b1000) != 0;
            inst->reg = b0 & 0b0111;
            inst->data = data[i++];

            if (inst->w == 1)
            {
                inst->data |= data[i++] << 8;
            }

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
dump_instruction (struct instruction *inst)
{
    char mod[128] = {0};
    char reg[128] = {0};
    char rm[128] = {0};

    printf ("d=%u w=%u s=%u mod=%s reg=%s r/m=%s disp=["BIN_FMT"  "BIN_FMT"] data=["BIN_FMT"  "BIN_FMT"]\n",
            inst->d, inst->w, inst->s,
            binary_print (mod, sizeof (mod), inst->mod),
            binary_print (reg, sizeof (reg), inst->reg),
            binary_print (rm, sizeof (rm), inst->rm),
            BIN_VAL ((inst->disp << 8)), BIN_VAL ((inst->disp & 0xFF)),
            BIN_VAL ((inst->data << 8)), BIN_VAL ((inst->data & 0xFF)));
}

static void
print_instruction (struct instruction *inst)
{
    switch (inst->type)
    {
        case MOV__REGISTER_TO_REGISTER:
        {
            char *dest = registers[inst->d ? inst->reg : inst->rm].array[inst->w];
            char *source = registers[inst->d ? inst->rm : inst->reg].array[inst->w];
            char *eac = eac_table[inst->rm];

            if (inst->mod == 0b11)
            {
                fprintf (fp, "%s %s, %s\n", type_str (inst->type), dest, source);
            }
            else if (inst->mod == 0b00)
            {
                fprintf (fp, "%s ", type_str (inst->type));
                if (inst->d)
                {
                    fprintf (fp, "%s, [%s]\n", dest, eac);
                }
                else
                {
                    fprintf (fp, "[%s], %s\n", eac, source);
                }
            }
            else
            {
                fprintf (fp, "%s ", type_str (inst->type));
                if (inst->d)
                {
                    fprintf (fp, "%s, [%s", dest, eac);
                    if (inst->disp)
                    {
                        if (inst->mod == 0b01)
                        {
                            fprintf (fp, " + %d", (s8) inst->disp);
                        }
                        else
                        {
                            fprintf (fp, " + %d", (s16) inst->disp);
                        }
                    }
                    fprintf (fp, "]\n");
                }
                else
                {
                    fprintf (fp, "[%s", eac);
                    if (inst->disp)
                    {
                        if (inst->mod == 0b01)
                        {
                            fprintf (fp, " + %d", (s8) inst->disp);
                        }
                        else
                        {
                            fprintf (fp, " + %d", (s16) inst->disp);
                        }
                    }
                    fprintf (fp, "], %s\n", source);
                }
            }
        } break;
        case MOV__IMMEDIATE_TO_REGISTER:
        {
            char *dest = registers[inst->reg].array[inst->w];

            fprintf (fp, "%s %s, %u\n", type_str (inst->type), dest, inst->data);
        } break;
        case ADD__REG_TO_REG:
        {
            char *dest = registers[inst->d ? inst->reg : inst->rm].array[inst->w];
            char *source = registers[inst->d ? inst->rm : inst->reg].array[inst->w];
            char *eac = eac_table[inst->rm];

#if 0
            dump_instruction (inst);
            fprintf (fp, "add %s, [%s]\n", dest, eac);
#endif
            if (inst->mod == 0b11)
            {
                fprintf (fp, "%s %s, %s\n", type_str (inst->type), dest, source);
            }
            else if (inst->mod == 0b00)
            {
                fprintf (fp, "%s ", type_str (inst->type));
                if (inst->d)
                {
                    fprintf (fp, "%s, [%s]\n", dest, eac);
                }
                else
                {
                    fprintf (fp, "[%s], %s\n", eac, source);
                }
            }
            else
            {
                fprintf (fp, "%s ", type_str (inst->type));
                if (inst->d)
                {
                    fprintf (fp, "%s, [%s", dest, eac);
                    if (inst->disp)
                    {
                        if (inst->mod == 0b01)
                        {
                            fprintf (fp, " + %d", (s8) inst->disp);
                        }
                        else
                        {
                            fprintf (fp, " + %d", (s16) inst->disp);
                        }
                    }
                    fprintf (fp, "]\n");
                }
                else
                {
                    fprintf (fp, "[%s", eac);
                    if (inst->disp)
                    {
                        if (inst->mod == 0b01)
                        {
                            fprintf (fp, " + %d", (s8) inst->disp);
                        }
                        else
                        {
                            fprintf (fp, " + %d", (s16) inst->disp);
                        }
                    }
                    fprintf (fp, "], %s\n", source);
                }
            }

        } break;
        case ADD__IMMEDIATE_TO_RM:
        {
            char *dest = registers[inst->d ? inst->reg : inst->rm].array[inst->w];
            char *source = registers[inst->d ? inst->rm : inst->reg].array[inst->w];
            char *eac = eac_table[inst->rm];

            dump_instruction (inst);

            if (inst->mod == 0b11)
            {
                fprintf (fp, "%s %s, %d\n", type_str (inst->type), dest, (s8) inst->data);
            }
            else if (inst->mod == 0b01 ||
                     inst->mod == 0b10)
            {
                fprintf (fp, "%s", type_str (inst->type));
                if (inst->w)
                {
                    fprintf (fp, " word");
                }
                else
                {
                    fprintf (fp, " byte");
                }

                fprintf (fp, " [%s", eac);
                if (inst->disp)
                {
                    fprintf (fp, " + %d", (s16) inst->disp);
                }
                fprintf (fp, "], %d\n", (s8) inst->data);
            }
            else
            {

                if (inst->w)
                {
                    fprintf (fp, "%s word [%s], %d\n", type_str (inst->type), eac, (s8) inst->data);
                }
                else
                {
                    fprintf (fp, "%s byte [%s], %d\n", type_str (inst->type), eac, (s8) inst->data);
                }
            }
        } break;
        case ADD__IMMEDIATE_TO_ACCUMULATOR:
        {
            char *dest = registers[inst->d ? inst->reg : inst->rm].array[inst->w];

            if (inst->w)
            {
                fprintf (fp, "%s %s, %d\n", type_str (inst->type), dest, (s16) inst->data);
            }
            else
            {
                fprintf (fp, "%s %s, %d\n", type_str (inst->type), dest, (s8) inst->data);
            }
        } break;
        default:
        {
            // do nothing
        } break;
    }
}

static u32
decode_add (u8 *data, struct instruction *inst)
{
    u32 i = 0;
    u32 bytes_consumed = 0;

    switch (inst->type)
    {
        case ADD__REG_TO_REG:
        {
            u8 b0 = data[i++];
            u8 b1 = data[i++];

            inst->d = (b0 & 0b10) != 0; // 0000 0010
            inst->w = (b0 & 0b01) != 0; // 0000 0001

            inst->mod = (b1 >> 6) & 0b11;  // 1100 0000
            inst->reg = (b1 >> 3) & 0b111; // 0011 1000
            inst->rm = b1 & 0b111;         // 0000 0111
            
            switch (inst->mod)
            {
                case 0b00:
                {
                    /**
                     * Memory mode, no displacements follows
                     * (Except when R/M = 110, then 16-bit
                     * displacement follows)
                     */
                    if (inst->rm == 0b110)
                    {
                        u8 b2 = data[i++];
                        u8 b3 = data[i++];
                        inst->disp = (b3 << 8) | b2;
                    }
                } break;
                case 0b01:
                {
                    /**
                     * Memory mode, 8-bit displacement follows
                     */
                    inst->disp = data[i++];
                } break;
                case 0b10:
                {
                    /**
                     * Memory mode, 16-bit displacement follows
                     */
                    u8 disp_low = data[i++];
                    u8 disp_high = data[i++];
                    inst->disp = (disp_high << 8) | disp_low;
                } break;
                case 0b11:
                {
                    /**
                     * Register mode (no displacement)
                     */
                } break;
                default:
                {
                    ASSERT (!"Invalid mod value");
                } break;
            }

            bytes_consumed = i;
        } break;
        case ADD__IMMEDIATE_TO_RM:
        {
            u8 b0 = data[i++];
            u8 b1 = data[i++];

            inst->s = (b0 & 0b10) != 0; // 0000 0010
            inst->w = (b0 & 0b01) != 0; // 0000 0001

            inst->mod = (b1 >> 6) & 0b11;  // 1100 0000
            inst->reg = (b1 >> 3) & 0b111; // 0011 1000
            inst->rm = b1 & 0b111;         // 0000 0111

            switch (inst->mod)
            {
                case 0b00:
                {
                    /**
                     * Memory mode, no displacements follows
                     * (Except when R/M = 110, then 16-bit
                     * displacement follows)
                     */
                    if (inst->rm == 0b110)
                    {
                        u8 b2 = data[i++];
                        u8 b3 = data[i++];
                        inst->disp = (b3 << 8) | b2;
                    }
                } break;
                case 0b01:
                {
                    /**
                     * Memory mode, 8-bit displacement follows
                     */
                    inst->disp = data[i++];
                } break;
                case 0b10:
                {
                    /**
                     * Memory mode, 16-bit displacement follows
                     */
                    u8 disp_low = data[i++];
                    u8 disp_high = data[i++];
                    inst->disp = (disp_high << 8) | disp_low;
                } break;
                case 0b11:
                {
                    /**
                     * Register mode (no displacement)
                     */
                } break;
                default:
                {
                    ASSERT (!"Invalid mod value");
                } break;
            }

            inst->data = data[i++];
            if (inst->s && inst->w)
            {
                /* sign extension? */
                inst->data |= 0xFF << 8;
            }
            bytes_consumed = i;
        } break;
        case ADD__IMMEDIATE_TO_ACCUMULATOR:
        {
            u8 b0 = data[i++];

            inst->w = (b0 & 0b01) != 0; // 0000 0001

            switch (inst->mod)
            {
                case 0b00:
                {
                    /**
                     * Memory mode, no displacements follows
                     * (Except when R/M = 110, then 16-bit
                     * displacement follows)
                     */
                    if (inst->rm == 0b110)
                    {
                        u8 b2 = data[i++];
                        u8 b3 = data[i++];
                        inst->disp = (b3 << 8) | b2;
                    }
                } break;
                case 0b01:
                {
                    /**
                     * Memory mode, 8-bit displacement follows
                     */
                    inst->disp = data[i++];
                } break;
                case 0b10:
                {
                    /**
                     * Memory mode, 16-bit displacement follows
                     */
                    u8 disp_low = data[i++];
                    u8 disp_high = data[i++];
                    inst->disp = (disp_high << 8) | disp_low;
                } break;
                case 0b11:
                {
                    /**
                     * Register mode (no displacement)
                     */
                } break;
                default:
                {
                    ASSERT (!"Invalid mod value");
                } break;
            }

            inst->data = data[i++];
            if (inst->w)
            {
                inst->data |= data[i++] << 8;
            }

            bytes_consumed = i;
        } break;
        default:
        {
            ASSERT (!"Unhandled path\n");
        }
    }

    return bytes_consumed;
}

static bool
is_mov (enum instruction_type type)
{
    bool ret;

    switch (type)
    {
        case MOV__REGISTER_TO_REGISTER:
        case MOV__IMMEDIATE_TO_MEMORY:
        case MOV__IMMEDIATE_TO_REGISTER:
        case MOV__MEMORY_TO_ACCUMULATOR:
        case MOV__ACCUMULATOR_TO_MEMORY:
        case MOV__RM_TO_SEG:
        case MOV__SEG_TO_RM:
        {
            ret = true;
        } break;
        default:
        {
            ret = false;
        } break;
    }

    return ret;
}

static bool
is_add (enum instruction_type type)
{
    bool ret;

    switch (type)
    {
        case ADD__REG_TO_REG:
        case ADD__IMMEDIATE_TO_RM:
        case ADD__IMMEDIATE_TO_ACCUMULATOR:
        {
            ret = true;
        } break;
        default:
        {
            ret = false;
        } break;
    }

    return ret;
}

static void
action_mov (u16 *registers, struct instruction *inst)
{
    
}

static void
decode (u8 *data, int len) // TODO: this should move to main()
{
    u32 bytes_consumed = 0;

    fprintf (fp, "; disassembly\n\n");
    fprintf (fp, "bits 16\n\n");

    enum regis {
        REGISTER_AX = 0,

        REGISTER_COUNT
    };
    u16 registers[REGISTER_COUNT] = {0};

    for (u32 i = 0; i < len; i += bytes_consumed)
    {
        u8 byte = data[i];
        struct instruction inst = {0};
        // TODO: store labels here?
        // TODO: need a cursor to track place in buffer

        inst.type = instruction_type_get (byte);
        if (is_mov (inst.type))
        {
            bytes_consumed = decode_mov (&data[i], &inst);
            action_mov (registers, &inst);
        }
        else if (is_add (inst.type))
        {
            bytes_consumed = decode_add (&data[i], &inst);
        }
        else
        {
            fprintf (stderr, "Error: Unknown instruction at byte %d ["BIN_FMT"]\n", i, BIN_VAL (byte));
            break;
        }

        print_instruction (&inst);
    }
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

        // TODO: allocate 1MB on start-up
        data = (u8 *) malloc (len);
        memset (data, 0, len);
        fread (data, len, 1, fp);

        *read_len = len;

        fclose (fp);
    }

    ok = (data && len > 0);
    ASSERT (ok);

#if 0
    debug ("Read [%s %d bytes] %s\n", file, len, (ok ? "OK" : "Error"));

    int n_bytes = 0;
    for (int i = 0; i < len; i++)
    {
        u8 byte = data[i];

        debug ("  0x%02X (" BIN_FMT ") ", byte, BIN_VAL (byte));

        if (++n_bytes == 2)
        {
            debug ("\n");
            n_bytes = 0;
        }
    }
    debug ("\n");
#endif

    return data;
}

static char *
parse_args (int argc, char **argv)
{
    char *ret = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp (argv[i], "-f") == 0)
        {
            if (i + 1 < argc)
            {
                fp = fopen (argv[i + 1], "w");
                i++;
            }
            else
            {
                fprintf (stderr, "Error: Missing filename parameter for argument '-f'\n");
                break;
            }
        }
        else
        {
            ret = argv[i];
            break;
        }
    }

    if (!ret)
    {
        fprintf (stderr, "Usage: [-f OUTPUT-FILE] INPUT-FILE\n");
    }

    return ret;
}

int
main (int argc, char **argv)
{
    fp = stdout;
    char *file = parse_args (argc, argv);

    if (file)
    {
        int len = 0;
        u8 *data = NULL;

        data = read_file (file, &len);
        if (data && len > 0)
        {
            decode (data, len);
            free (data);
        }
    }

    if (fp && fp != stdout)
    {
        fclose (fp);
    }

    return 0;
}
