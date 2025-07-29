#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h> // Para as funções trigonométricas
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/adc.h" // Incluído para ADC, mas pode ser removido se não for usado no datalogger
#include "hardware/rtc.h"
#include "pico/stdlib.h"

#include "ff.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "my_debug.h"
#include "rtc.h"
#include "sd_card.h"

// Definição dos pinos I2C para o MPU6050
#define I2C_PORT i2c0 // I2C0 usa pinos 0 e 1
#define I2C_SDA 0
#define I2C_SCL 1

// Definição dos pinos I2C para o display OLED
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define ENDERECO_DISP 0x3C // Endereço I2C do display

// Endereço padrão do MPU6050
static int addr = 0x68;

#define ADC_PIN 26 // GPIO 26 - Não será usado no datalogger do MPU6050

// Variáveis globais para o datalogger
static FIL log_file; // Objeto FIL para o arquivo de log
static bool file_open = false; // Flag para indicar se o arquivo está aberto
static char log_filename[30] = "mpu_data.csv"; // Nome do arquivo de log do MPU6050

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

// Funções do FatFS (mantidas as originais, mas adaptadas algumas)
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
        //return -1; // Adicionado para indicar erro
    }
    tot_sect = (p_fs->n_fatent - 2) * p_fs->csize;
    fre_sect = fre_clust * p_fs->csize;
    printf("%10lu KiB total drive space.\n%10lu KiB available.\n", tot_sect / 2, fre_sect / 2);
    //return 0; // Adicionado para indicar sucesso
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

// NOVO: Função para inicializar o arquivo de log do MPU6050
bool init_mpu_logger()
{
    if (file_open) {
        printf("[INFO] Arquivo de log do MPU já está aberto.\n");
        return true;
    }

    // Tenta montar o cartão SD se ainda não estiver montado
    sd_card_t *pSD = sd_get_by_name("0:"); // Assumindo "0:" como o nome do drive padrão
    if (!pSD || !pSD->mounted) {
        printf("[INFO] Cartão SD não montado. Tentando montar...\n");
        run_mount(); // Tenta montar o SD
        sleep_ms(1000); // Dá um tempo para a montagem
        if (!pSD || !pSD->mounted) {
            printf("[ERRO] Falha ao montar o cartão SD. Não é possível iniciar o log.\n");
            return false;
        }
    }

    FRESULT res = f_open(&log_file, log_filename, FA_WRITE | FA_OPEN_ALWAYS | FA_READ); // Abrir para escrita, criar se não existir, e permitir leitura para verificar tamanho
    if (res != FR_OK)
    {
        printf("[ERRO] Não foi possível abrir/criar o arquivo '%s' para escrita: %s (%d)\n", log_filename, FRESULT_str(res), res);
        return false;
    }

    // Verificar se o arquivo está vazio para escrever o cabeçalho
    if (f_size(&log_file) == 0)
    {
        char header_buffer[] = "Timestamp,AccX,AccY,AccZ,GyroX,GyroY,GyroZ\n"; // Cabeçalho com timestamp
        UINT bw_header;
        res = f_write(&log_file, header_buffer, strlen(header_buffer), &bw_header);
        if (res != FR_OK)
        {
            printf("[ERRO] Não foi possível escrever o cabeçalho no arquivo: %s (%d)\n", FRESULT_str(res), res);
            f_close(&log_file); // Fecha o arquivo em caso de erro
            return false;
        }
    }

    printf("[INFO] Arquivo de log do MPU '%s' aberto com sucesso.\n", log_filename);
    file_open = true;
    return true;
}

// NOVO: Função para capturar e salvar dados do MPU6050
void log_mpu6050_data(int16_t accel[3], int16_t gyro[3])
{
    if (!file_open) {
        printf("[ALERTA] Arquivo de log do MPU não está aberto. Ignorando dados.\n");
        return;
    }

    datetime_t t;
    rtc_get_datetime(&t); // Obtém a hora atual do RTC

    char data_buffer[100]; // Buffer para a linha de dados

    // Formata a string de dados
    sprintf(data_buffer, "%04d-%02d-%02d %02d:%02d:%02d,%d,%d,%d,%d,%d,%d\n",
            t.year, t.month, t.day, t.hour, t.min, t.sec,
            accel[0], accel[1], accel[2],
            gyro[0], gyro[1], gyro[2]);

    UINT bw;
    FRESULT res = f_write(&log_file, data_buffer, strlen(data_buffer), &bw);
    if (res != FR_OK)
    {
        printf("[ERRO] Falha ao escrever dados do MPU no arquivo: %s (%d)\n", FRESULT_str(res), res);
        // Opcional: tentar fechar e reabrir o arquivo em caso de erro crítico
        f_close(&log_file);
        file_open = false;
        printf("[ERRO] Arquivo de log do MPU fechado devido a erro de escrita.\n");
    } else {
        f_sync(&log_file); // Força a escrita dos dados para o SD card imediatamente
    }
}

// NOVO: Função para fechar o arquivo de log do MPU6050
void close_mpu_logger() {
    if (file_open) {
        FRESULT res = f_close(&log_file);
        if (res == FR_OK) {
            printf("[INFO] Arquivo de log do MPU fechado com sucesso.\n");
        } else {
            printf("[ERRO] Falha ao fechar o arquivo de log do MPU: %s (%d)\n", FRESULT_str(res), res);
        }
        file_open = false;
    } else {
        printf("[INFO] Arquivo de log do MPU já está fechado.\n");
    }
}


// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    // Antes de resetar, tente fechar o arquivo de log para evitar corrupção
    close_mpu_logger();
    printf("Entrando em modo BOOTSEL...\n");
    sleep_ms(100); // Pequeno atraso para garantir a mensagem
    reset_usb_boot(0, 0);
}

static void run_help()
{
    printf("\nComandos disponíveis:\n\n");
    printf("Digite 'm' para montar o cartão SD\n");
    printf("Digite 'u' para desmontar o cartão SD\n");
    printf("Digite 'l' para listar arquivos\n");
    printf("Digite 't' para mostrar conteúdo do arquivo de log do MPU (%s)\n", log_filename); // Adaptado
    printf("Digite 'e' para obter espaço livre no cartão SD\n");
    printf("Digite 'f' para formatar o cartão SD\n");
    printf("Digite 's' para iniciar o log do MPU (cria/abre o arquivo)\n"); // Novo comando
    printf("Digite 'x' para parar e fechar o log do MPU\n"); // Novo comando
    printf("Digite 'h' para exibir os comandos disponíveis\n");
    printf("\nEscolha o comando: ");
}

typedef void (*p_fn_t)();
typedef struct
{
    char const *const command;
    p_fn_t const function;
    char const *const help;
} cmd_def_t;

// NOTA: Os comandos abaixo são para o console serial, não para o loop contínuo de log.
// Para um datalogger contínuo, a lógica de log estará no main loop.
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
    sleep_ms(2000); // Reduzido o delay inicial
    printf("Iniciando Datalogger MPU6050...\n");

    time_init(); // Inicializa o RTC
    rtc_init(); // Habilita o hardware do RTC

    // Opcional: Defina a data e hora do RTC aqui se não quiser usar o comando 'setrtc'
    // datetime_t initial_time = { .year = 2025, .month = 7, .day = 29, .dotw = 2, .hour = 10, .min = 0, .sec = 0};
    // rtc_set_datetime(&initial_time);
    // printf("RTC configurado para 29/07/2025 10:00:00\n");

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
    uint32_t log_interval_ms = 100; // Intervalo de log em milissegundos (100ms = 10Hz)
    absolute_time_t last_log_time = get_absolute_time(); // Para controle de tempo

    printf("\n> ");
    stdio_flush();
    run_help(); // Exibe os comandos disponíveis no início

    while (true)
    {
        // === Lógica para o Datalogger contínuo do MPU6050 ===
        if (file_open && absolute_time_diff_us(last_log_time, get_absolute_time()) / 1000 >= log_interval_ms)
        {
            mpu6050_read_raw(aceleracao, gyro, &temp); // Lê os dados do MPU
            log_mpu6050_data(aceleracao, gyro);        // Salva os dados no arquivo
            last_log_time = get_absolute_time();       // Atualiza o último tempo de log
        }
        // === Fim da Lógica do Datalogger ===


        // Leitura dos dados de aceleração, giroscópio e temperatura (para o display)
        mpu6050_read_raw(aceleracao, gyro, &temp);

        // Conversão para unidade de 'g'
        float ax = aceleracao[0] / 16384.0f;
        float ay = aceleracao[1] / 16384.0f;
        float az = aceleracao[2] / 16384.0f;

        // Cálculo dos ângulos em graus (Roll e Pitch)
        float roll = atan2(ay, az) * 180.0f / M_PI;
        float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / M_PI;

        // Montagem das strings para o display
        char str_roll[20];
        char str_pitch[20];

        snprintf(str_roll, sizeof(str_roll), "%5.1f", roll);
        snprintf(str_pitch, sizeof(str_pitch), "%5.1f", pitch);

        // Exibição no display
        ssd1306_fill(&ssd, !cor);                        // Limpa o display
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);    // Desenha um retângulo
        ssd1306_line(&ssd, 3, 25, 123, 25, cor);         // Desenha uma linha horizontal
        ssd1306_line(&ssd, 3, 37, 123, 37, cor);         // Desenha outra linha horizontal
        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6); // Escreve texto no display
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);    // Escreve texto no display
        ssd1306_draw_string(&ssd, "IMU    MPU6050", 10, 28); // Escreve texto no display
        ssd1306_line(&ssd, 63, 35, 63, 60, cor);         // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, "roll", 14, 41);          // Escreve texto no display
        ssd1306_draw_string(&ssd, str_roll, 14, 52);       // Exibe Roll
        ssd1306_draw_string(&ssd, "pitch", 73, 41);         // Escreve texto no display
        ssd1306_draw_string(&ssd, str_pitch, 73, 52);      // Exibe Pitch
        ssd1306_send_data(&ssd);
        sleep_ms(50); // Reduzido o delay para que o display atualize mais rápido e o terminal não fique tão "preso"

        int cRxedChar = getchar_timeout_us(0);
        if (PICO_ERROR_TIMEOUT != cRxedChar)
            process_stdio(cRxedChar);

        // === Novos Comandos para o Datalogger no Terminal ===
        if (cRxedChar == 'm') // Monta o SD card se pressionar 'm'
        {
            printf("\nMontando o SD...\n");
            run_mount();
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 'u') // Desmonta o SD card se pressionar 'u'
        {
            printf("\nDesmontando o SD. Aguarde...\n");
            close_mpu_logger(); // Tenta fechar o arquivo de log antes de desmontar
            run_unmount();
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 'l') // Lista diretórios e os arquivos se pressionar 'l'
        {
            printf("\nListagem de arquivos no cartão SD.\n");
            run_ls();
            printf("\nListagem concluída.\n");
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 't') // Exibe o conteúdo do arquivo de log do MPU
        {
            read_file(log_filename); // Usa o nome de arquivo do MPU
            printf("Escolha o comando (h = help): ");
        }
        if (cRxedChar == 'e') // Obtém o espaço livre no SD card se pressionar 'e'
        {
            printf("\nObtendo espaço livre no SD.\n\n");
            run_getfree();
            printf("\nEspaço livre obtido.\n");
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 'f') // Formata o SD card se pressionar 'f'
        {
            printf("\nProcesso de formatação do SD iniciado. Aguarde...\n");
            close_mpu_logger(); // Tenta fechar o arquivo de log antes de formatar
            run_format();
            printf("\nFormatação concluída.\n\n");
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 's') // Inicia o log do MPU
        {
            printf("\nIniciando o log de dados do MPU...\n");
            init_mpu_logger();
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 'x') // Para o log do MPU
        {
            printf("\nParando e fechando o log de dados do MPU...\n");
            close_mpu_logger();
            printf("\nEscolha o comando (h = help): ");
        }
        if (cRxedChar == 'h') // Exibe os comandos disponíveis se pressionar 'h'
        {
            run_help();
        }
    }
    close_mpu_logger(); // Garante que o arquivo é fechado ao sair do main (embora o while(true) seja infinito)
    return 0;
}