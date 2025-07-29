#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>                    // Para as funções trigonométricas
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

// Definição dos pinos I2C para o MPU6050
#define I2C_PORT i2c0                 // I2C0 usa pinos 0 e 1
#define I2C_SDA 0
#define I2C_SCL 1

// Definição dos pinos I2C para o display OLED
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define ENDERECO_DISP 0x3C            // Endereço I2C do display

#define ACCEL_SCALE_FACTOR 16384.0f // Para ±2g
#define GYRO_SCALE_FACTOR  131.0f   // Para ±250°/s

// Endereço padrão do MPU6050
static int addr = 0x68;
#include "hardware/adc.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"

#include "ff.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "my_debug.h"
#include "rtc.h"
#include "sd_card.h"

#define ADC_PIN 26 // GPIO 26
// Função para resetar e inicializar o MPU6050
static void mpu6050_reset()
{
    // Dois bytes para reset: primeiro o registrador, segundo o dado
    uint8_t buf[] = {0x6B, 0x80};
    i2c_write_blocking(I2C_PORT, addr, buf, 2, false);
    sleep_ms(100); // Aguarda reset e estabilização

    // Sai do modo sleep (registrador 0x6B, valor 0x00)
    buf[1] = 0x00;
    i2c_write_blocking(I2C_PORT, addr, buf, 2, false);
    sleep_ms(10); // Aguarda estabilização após acordar
}

// Função para ler dados crus do acelerômetro, giroscópio e temperatura
static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp)
{
    uint8_t buffer[6];

    // Lê aceleração a partir do registrador 0x3B (6 bytes)
    uint8_t val = 0x3B;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++)
    {
        accel[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }

    // Lê giroscópio a partir do registrador 0x43 (6 bytes)
    val = 0x43;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 6, false);

    for (int i = 0; i < 3; i++)
    {
        gyro[i] = (buffer[i * 2] << 8) | buffer[(i * 2) + 1];
    }

    // Lê temperatura a partir do registrador 0x41 (2 bytes)
    val = 0x41;
    i2c_write_blocking(I2C_PORT, addr, &val, 1, true);
    i2c_read_blocking(I2C_PORT, addr, buffer, 2, false);

    *temp = (buffer[0] << 8) | buffer[1];
}


static bool logger_enabled;
static const uint32_t period = 1000;
static absolute_time_t next_log_time;

static char filename[20] = "Dalalogger_data1.csv";

static sd_card_t *sd_get_by_name(const char *const name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return sd_get_by_num(i);
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}
static FATFS *sd_get_fs_by_name(const char *name)
{
    for (size_t i = 0; i < sd_get_num(); ++i)
        if (0 == strcmp(sd_get_by_num(i)->pcName, name))
            return &sd_get_by_num(i)->fatfs;
    DBG_PRINTF("%s: unknown name %s\n", __func__, name);
    return NULL;
}


static void run_setrtc()
{
    const char *dateStr = strtok(NULL, " ");
    if (!dateStr)
    {
        printf("Missing argument\n");
        return;
    }
    int date = atoi(dateStr);

    const char *monthStr = strtok(NULL, " ");
    if (!monthStr)
    {
        printf("Missing argument\n");
        return;
    }
    int month = atoi(monthStr);

    const char *yearStr = strtok(NULL, " ");
    if (!yearStr)
    {
        printf("Missing argument\n");
        return;
    }
    int year = atoi(yearStr) + 2000;

    const char *hourStr = strtok(NULL, " ");
    if (!hourStr)
    {
        printf("Missing argument\n");
        return;
    }
    int hour = atoi(hourStr);

    const char *minStr = strtok(NULL, " ");
    if (!minStr)
    {
        printf("Missing argument\n");
        return;
    }
    int min = atoi(minStr);

    const char *secStr = strtok(NULL, " ");
    if (!secStr)
    {
        printf("Missing argument\n");
        return;
    }
    int sec = atoi(secStr);

    datetime_t t = {
        .year = (int16_t)year,
        .month = (int8_t)month,
        .day = (int8_t)date,
        .dotw = 0, // 0 is Sunday
        .hour = (int8_t)hour,
        .min = (int8_t)min,
        .sec = (int8_t)sec};
    rtc_set_datetime(&t);
}

static void run_format()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    /* Format the drive with default parameters */
    FRESULT fr = f_mkfs(arg1, 0, 0, FF_MAX_SS * 2);
    if (FR_OK != fr)
        printf("f_mkfs error: %s (%d)\n", FRESULT_str(fr), fr);
}
static void run_mount()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    FRESULT fr = f_mount(p_fs, arg1, 1);
    if (FR_OK != fr)
    {
        printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = true;
    printf("Processo de montagem do SD ( %s ) concluído\n", pSD->pcName);
}
static void run_unmount()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    FRESULT fr = f_unmount(arg1);
    if (FR_OK != fr)
    {
        printf("f_unmount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    sd_card_t *pSD = sd_get_by_name(arg1);
    myASSERT(pSD);
    pSD->mounted = false;
    pSD->m_Status |= STA_NOINIT; // in case medium is removed
    printf("SD ( %s ) desmontado\n", pSD->pcName);
}
static void run_getfree()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = sd_get_by_num(0)->pcName;
    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *p_fs = sd_get_fs_by_name(arg1);
    if (!p_fs)
    {
        printf("Unknown logical drive number: \"%s\"\n", arg1);
        return;
    }
    FRESULT fr = f_getfree(arg1, &fre_clust, &p_fs);
    if (FR_OK != fr)
    {
        printf("f_getfree error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    tot_sect = (p_fs->n_fatent - 2) * p_fs->csize;
    fre_sect = fre_clust * p_fs->csize;
    printf("%10lu KiB total drive space.\n%10lu KiB available.\n", tot_sect / 2, fre_sect / 2);
}
static void run_ls()
{
    const char *arg1 = strtok(NULL, " ");
    if (!arg1)
        arg1 = "";
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr;
    char const *p_dir;
    if (arg1[0])
    {
        p_dir = arg1;
    }
    else
    {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr)
        {
            printf("f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }
        p_dir = cwdbuf;
    }
    printf("Directory Listing: %s\n", p_dir);
    DIR dj;
    FILINFO fno;
    memset(&dj, 0, sizeof dj);
    memset(&fno, 0, sizeof fno);
    fr = f_findfirst(&dj, &fno, p_dir, "*");
    if (FR_OK != fr)
    {
        printf("f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (fr == FR_OK && fno.fname[0])
    {
        const char *pcWritableFile = "writable file",
                   *pcReadOnlyFile = "read only file",
                   *pcDirectory = "directory";
        const char *pcAttrib;
        if (fno.fattrib & AM_DIR)
            pcAttrib = pcDirectory;
        else if (fno.fattrib & AM_RDO)
            pcAttrib = pcReadOnlyFile;
        else
            pcAttrib = pcWritableFile;
        printf("%s [%s] [size=%llu]\n", fno.fname, pcAttrib, fno.fsize);

        fr = f_findnext(&dj, &fno);
    }
    f_closedir(&dj);
}
static void run_cat()
{
    char *arg1 = strtok(NULL, " ");
    if (!arg1)
    {
        printf("Missing argument\n");
        return;
    }
    FIL fil;
    FRESULT fr = f_open(&fil, arg1, FA_READ);
    if (FR_OK != fr)
    {
        printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    char buf[256];
    while (f_gets(buf, sizeof buf, &fil))
    {
        printf("%s", buf);
    }
    fr = f_close(&fil);
    if (FR_OK != fr)
        printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
}

// Função para capturar dados e salvar no arquivo *.csv
void capture_data_and_save()
{
    printf("\nCapturando dados do . Aguarde finalização...\n");
    FIL file;
    FRESULT res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK)
    {
        printf("\n[ERRO] Não foi possível abrir o arquivo para escrita. Monte o Cartao.\n");
        return;
    }
    //char header_buffer[] = "Amostra,ADC_Value\n"; // Define o cabeçalho
    char header_buffer[] = "amostra,AccX,AccY,AccZ,GyroX,GyroY,GyroZ\n"; // Define o cabeçalho para dados do MPU6050
    UINT bw_header;
    res = f_write(&file, header_buffer, strlen(header_buffer), &bw_header);
    if (res != FR_OK)
    {
        printf("[ERRO] Não foi possível escrever o cabeçalho no arquivo. Monte o Cartao.\n");
        f_close(&file);
        return;
    }
    // --- FIM DA ADIÇÃO DO CABEÇALHO ---
    // Variáveis para armazenar as leituras do MPU6050
    int16_t aceleracao[3], gyro[3], temp_mpu; // 'temp_mpu' para não confundir com outras variáveis 'temp'
    float normalized_accel[3];
    float normalized_gyro[3];

    for (int i = 0; i < 128; i++)
    {
        // === LEITURA DOS DATOS DO MPU6050 ===
        mpu6050_read_raw(aceleracao, gyro, &temp_mpu); // Captura os dados brutos do MPU6050

        // === NORMALIZAÇÃO DOS DADOS ===
        normalized_accel[0] = (float)aceleracao[0] / ACCEL_SCALE_FACTOR;
        normalized_accel[1] = (float)aceleracao[1] / ACCEL_SCALE_FACTOR;
        normalized_accel[2] = (float)aceleracao[2] / ACCEL_SCALE_FACTOR;

        normalized_gyro[0] = (float)gyro[0] / GYRO_SCALE_FACTOR;
        normalized_gyro[1] = (float)gyro[1] / GYRO_SCALE_FACTOR;
        normalized_gyro[2] = (float) gyro[2]  / GYRO_SCALE_FACTOR;

        // Buffer para a linha de dados (aumentado para acomodar floats com casas decimais)
        char data_buffer[150]; // Aumentado para garantir espaço para valores float formatados

        // Formata a string de dados no formato CSV, AGORA COM %f E AS NOVAS VARIÁVEIS FLOAT
        sprintf(data_buffer, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                i + 1, // Número da amostra (começa em 1)
                normalized_accel[0], normalized_accel[1], normalized_accel[2], // Valores float de aceleração
                normalized_gyro[0], normalized_gyro[1], normalized_gyro[2]);   // Valores float de giroscópio

        // === ESCRITA NO ARQUIVO ===
        UINT bw;
        res = f_write(&file, data_buffer, strlen(data_buffer), &bw);
        if (res != FR_OK)
        {
            printf("[ERRO] Não foi possível escrever dados do MPU no arquivo: %s (%d)\n", FRESULT_str(res), res);
            f_close(&file);
            return;
        }
        f_sync(&file); // Força a escrita do buffer para o cartão SD imediatamente

        sleep_ms(100); // Intervalo entre as amostras (100ms = 10Hz)
    }
    f_close(&file);
    printf("\nDados salvos no arquivo %s.\n\n", filename);
    
}

// Função para ler o conteúdo de um arquivo e exibir no terminal
void read_file(const char *filename)
{
    FIL file;
    FRESULT res = f_open(&file, filename, FA_READ);
    if (res != FR_OK)
    {
        printf("[ERRO] Não foi possível abrir o arquivo para leitura. Verifique se o Cartão está montado ou se o arquivo existe.\n");

        return;
    }
    char buffer[128];
    UINT br;
    printf("Conteúdo do arquivo %s:\n", filename);
    while (f_read(&file, buffer, sizeof(buffer) - 1, &br) == FR_OK && br > 0)
    {
        buffer[br] = '\0';
        printf("%s", buffer);
    }
    f_close(&file);
    printf("\nLeitura do arquivo %s concluída.\n\n", filename);
}

// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

static void run_help()
{
    printf("\nComandos disponíveis:\n\n");
    printf("Digite 'a' para montar o cartão SD\n");
    printf("Digite 'b' para desmontar o cartão SD\n");
    printf("Digite 'c' para listar arquivos\n");
    printf("Digite 'd' para mostrar conteúdo do arquivo\n");
    printf("Digite 'e' para obter espaço livre no cartão SD\n");
    printf("Digite 'f' para capturar dados e salvar no arquivo\n");
    printf("Digite 'g' para formatar o cartão SD\n");
    printf("Digite 'h' para exibir os comandos disponíveis\n");
    printf("\nEscolha o comando:  ");
}

typedef void (*p_fn_t)();
typedef struct
{
    char const *const command;
    p_fn_t const function;
    char const *const help;
} cmd_def_t;

static cmd_def_t cmds[] = {
    {"setrtc", run_setrtc, "setrtc <DD> <MM> <YY> <hh> <mm> <ss>: Set Real Time Clock"},
    {"format", run_format, "format [<drive#:>]: Formata o cartão SD"},
    {"mount", run_mount, "mount [<drive#:>]: Monta o cartão SD"},
    {"unmount", run_unmount, "unmount <drive#:>: Desmonta o cartão SD"},
    {"getfree", run_getfree, "getfree [<drive#:>]: Espaço livre"},
    {"ls", run_ls, "ls: Lista arquivos"},
    {"cat", run_cat, "cat <filename>: Mostra conteúdo do arquivo"},
    {"help", run_help, "help: Mostra comandos disponíveis"}};

static void process_stdio(int cRxedChar)
{
    static char cmd[256];
    static size_t ix;

    if (!isprint(cRxedChar) && !isspace(cRxedChar) && '\r' != cRxedChar &&
        '\b' != cRxedChar && cRxedChar != (char)127)
        return;
    printf("%c", cRxedChar); // echo
    stdio_flush();
    if (cRxedChar == '\r')
    {
        printf("%c", '\n');
        stdio_flush();

        if (!strnlen(cmd, sizeof cmd))
        {
            printf("> ");
            stdio_flush();
            return;
        }
        char *cmdn = strtok(cmd, " ");
        if (cmdn)
        {
            size_t i;
            for (i = 0; i < count_of(cmds); ++i)
            {
                if (0 == strcmp(cmds[i].command, cmdn))
                {
                    (*cmds[i].function)();
                    break;
                }
            }
            if (count_of(cmds) == i)
                printf("Command \"%s\" not found\n", cmdn);
        }
        ix = 0;
        memset(cmd, 0, sizeof cmd);
        printf("\n> ");
        stdio_flush();
    }
    else
    {
        if (cRxedChar == '\b' || cRxedChar == (char)127)
        {
            if (ix > 0)
            {
                ix--;
                cmd[ix] = '\0';
            }
        }
        else
        {
            if (ix < sizeof cmd - 1)
            {
                cmd[ix] = cRxedChar;
                ix++;
            }
        }
    }
}

int main()
{
    // Para ser utilizado o modo BOOTSEL com botão B
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    sleep_ms(5000);
    time_init();
    adc_init();

    // Inicializa a I2C do Display OLED em 400kHz
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

     ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO_DISP, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicialização da I2C do MPU6050
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Declara os pinos como I2C na Binary Info
    bi_decl(bi_2pins_with_func(I2C_SDA, I2C_SCL, GPIO_FUNC_I2C));
    mpu6050_reset();

    int16_t aceleracao[3], gyro[3], temp;

    bool cor = true;    


    printf("FatFS SPI example\n");
    printf("\033[2J\033[H"); // Limpa tela
    printf("\n> ");
    stdio_flush();
    //    printf("A tela foi limpa...\n");
    //    printf("Depois do Flush\n");
    run_help();
    while (true)
    {
        
        // Cálculo dos ângulos em graus (Roll e Pitch)
        float roll  =  180;
        float pitch = 130.0;

        // Montagem das strings para o display
        char str_roll[20];
        char str_pitch[20];

        
        snprintf(str_roll,  sizeof(str_roll),  "%5.1f", roll);
        snprintf(str_pitch, sizeof(str_pitch), "%5.1f", pitch);
        

        // Exibição no display
        ssd1306_fill(&ssd, !cor);                            // Limpa o display
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);        // Desenha um retângulo
        ssd1306_line(&ssd, 3, 25, 123, 25, cor);             // Desenha uma linha horizontal
        ssd1306_line(&ssd, 3, 37, 123, 37, cor);             // Desenha outra linha horizontal
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6);   // Escreve texto no display
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);    // Escreve texto no display
        ssd1306_draw_string(&ssd, "IMU    MPU6050", 10, 28); // Escreve texto no display
        ssd1306_line(&ssd, 63, 35, 63, 60, cor);             // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, "roll", 14, 41);           // Escreve texto no display
        ssd1306_draw_string(&ssd, str_roll, 14, 52);         // Exibe Roll
        ssd1306_draw_string(&ssd, "pitch", 73, 41);           // Escreve texto no display        
        ssd1306_draw_string(&ssd, str_pitch, 73, 52);        // Exibe Pitch
        ssd1306_send_data(&ssd);
        sleep_ms(500);
        int cRxedChar = getchar_timeout_us(0);
        if (PICO_ERROR_TIMEOUT != cRxedChar)
            process_stdio(cRxedChar);

        if (cRxedChar == 'a') // Monta o SD card se pressionar 'a'
        {
            printf("\nMontando o SD...\n");
            run_mount();
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'b') // Desmonta o SD card se pressionar 'b'
        {
            printf("\nDesmontando o SD. Aguarde...\n");
            run_unmount();
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'c') // Lista diretórios e os arquivos se pressionar 'c'
        {
            printf("\nListagem de arquivos no cartão SD.\n");
            run_ls();
            printf("\nListagem concluída.\n");
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'd') // Exibe o conteúdo do arquivo se pressionar 'd'
        {
            read_file(filename);
            printf("Escolha o comando (h = help):  ");
        }
        if (cRxedChar == 'e') // Obtém o espaço livre no SD card se pressionar 'e'
        {
            printf("\nObtendo espaço livre no SD.\n\n");
            run_getfree();
            printf("\nEspaço livre obtido.\n");
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'f') // Captura dados e salva no arquivo se pressionar 'f'
        {
            capture_data_and_save();
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'g') // Formata o SD card se pressionar 'g'
        {
            printf("\nProcesso de formatação do SD iniciado. Aguarde...\n");
            run_format();
            printf("\nFormatação concluída.\n\n");
            printf("\nEscolha o comando (h = help):  ");
        }
        if (cRxedChar == 'h') // Exibe os comandos disponíveis se pressionar 'h'
        {
            run_help();
        }
        sleep_ms(500);
    }
    return 0;
}