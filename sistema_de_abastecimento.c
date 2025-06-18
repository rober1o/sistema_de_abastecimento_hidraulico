#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"
#define LED_PIN 12
#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_C 22
#define PIN_BOMBA 28
#define Bomba 8 // simula bomba
#define LED_BOMBA_LIGADA 16
#define LED_BOMBA_DESLIGADA 17
#define BUZZER_PIN 10

// Variaveis PWM
#define PWM_WRAP 255

// Tempo mínimo entre acionamentos (200 ms)
const uint64_t DEBOUNCE_TIME_US = 200000;

uint64_t ultimo_som_baixo_us = 0;
uint64_t ultimo_som_alto_us = 0;
bool som_baixo_tocado = false;
bool som_alto_tocado = false;
const uint64_t intervalo_som_us = 5000000; // 5 segundos em microssegundos

// Últimos tempos de acionamento de cada botão
uint64_t ultimo_tempo_a = 0;
uint64_t ultimo_tempo_b = 0;
uint64_t ultimo_tempo_c = 0;
int calibracao_minima = 400;
int calibracao_maxima = 2050;
// Variáveis globais para armazenar min e max
int limite_min = 20;
int limite_max = 80;

void tocar_nivel_alto();
void tocar_nivel_baixo();
void tocar_start();

void LigDes_bomba()
{

    adc_select_input(2);
    uint16_t x = adc_read();
    uint16_t nivel = (uint16_t)((1.0f - ((float)(x - calibracao_minima) / (calibracao_maxima - calibracao_minima))) * 100.0f);
    printf("nivel %d\n", nivel);

    // Controle da bomba e sons com lógica de estado
    if (nivel <= limite_min)
    {
        gpio_put(Bomba, 1); // Liga bomba

        // Toca apenas se ainda não tiver tocado para este evento
        if (!som_baixo_tocado)
        {
            tocar_nivel_baixo();
            som_baixo_tocado = true;
            som_alto_tocado = false; // Reseta o estado do som alto
        }
    }
    else if (nivel >= limite_max)
    {
        gpio_put(Bomba, 0); // Desliga bomba

        // Toca apenas se ainda não tiver tocado para este evento
        if (!som_alto_tocado)
        {
            tocar_nivel_alto();
            som_alto_tocado = true;
            som_baixo_tocado = false; // Reseta o estado do som baixo
        }
    }
    else
    {
        // Dentro da faixa normal - reseta os estados
        som_baixo_tocado = false;
        som_alto_tocado = false;
    }

    if (gpio_get(Bomba))
    {
        gpio_put(LED_BOMBA_DESLIGADA, false);
        gpio_put(LED_BOMBA_LIGADA, true);
    }
    else
    {
        gpio_put(LED_BOMBA_LIGADA, false);
        gpio_put(LED_BOMBA_DESLIGADA, true);
    }
}

// Callback para parar o tom
int64_t stop_tone_callback(alarm_id_t id, void *user_data)
{
    uint gpio = *(uint *)user_data;
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_enabled(slice_num, false);
    gpio_set_function(gpio, GPIO_FUNC_SIO);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0);
    free(user_data);
    return 0;
}
// Feedback sonoro
void play_tone_non_blocking(uint gpio, int frequency, int duration_ms)
{
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    float divider = 1.0f;
    uint32_t wrap = (uint32_t)((125000000.0f / (divider * frequency)) - 1);
    pwm_set_clkdiv(slice_num, divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, channel, (wrap + 1) / 2);
    pwm_set_enabled(slice_num, true);
    uint *gpio_ptr = malloc(sizeof(uint));
    *gpio_ptr = gpio;
    add_alarm_in_ms(duration_ms, stop_tone_callback, gpio_ptr, false);
}

// Reproduzir som para inicialização do sistema
void tocar_start()
{

    play_tone_non_blocking(BUZZER_PIN, 523, 200); // Dó

    play_tone_non_blocking(BUZZER_PIN, 659, 200); // Mi

    play_tone_non_blocking(BUZZER_PIN, 784, 400); // Sol
}

// Reproduzir som para nível alto (5 segundos)
void tocar_nivel_alto()
{
    play_tone_non_blocking(BUZZER_PIN, 440, 1000); // Lá por 5 segundos
}

// Reproduzir som para nível baixo (5 segundos)
void tocar_nivel_baixo()
{
    play_tone_non_blocking(BUZZER_PIN, 440, 1000); // Dó por 5 segundos
}

#define WIFI_SSID "GIPAR"
#define WIFI_PASS "usergipar"

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Sistema de bastecimento</title><style>"
    "body{font-family:sans-serif;background:#f0f0f0;text-align:center;padding:20px;}"
    ".j{background:#125CC3;padding:20px;border-radius:10px;margin:0 auto;max-width:50vw;color:#fff;}"
    ".card{background:#fff;color:#333;padding:10px;margin:10px auto;border-radius:8px;width:90%;}"
    ".card.small{width:40%;display:inline-block;margin:10px 28px;}"
    ".bar{width:100%;background:#ddd;border-radius:8px;overflow:hidden;height:25px;margin:15px 0;}"
    ".fill{height:100%;background:#07B;width:0%;transition:0.5s;}"
    "input{width:90px;padding:6px;margin:10px 5px;border:1px solid #ccc;border-radius:5px;font-size:14px;}"
    "button{padding:8px 16px;background:#FA0;color:#fff;border:0;border-radius:5px;cursor:pointer;margin:10px;width:160px;font-size:1.2em;}"
    "#feedback{font-size:14px;font-weight:bold;padding:8px 16px;border-radius:8px;margin-top:15px;display:inline-block;transition:opacity 0.5s ease;opacity:0;}"
    "#feedback.ok{background:rgba(76,175,80,0.8);color:#fff;}"
    "#feedback.error{background:rgba(244,67,54,0.8);color:#fff;}"
    "</style><script>"
    "function att(){fetch('/estado').then(r=>r.json()).then(d=>{"
    "document.getElementById('bomba').innerText=d.bomba?'Ligada':'Desligada';"
    "document.getElementById('min').innerText=d.min;"
    "document.getElementById('max').innerText=d.max;"
    "document.getElementById('nivel').innerText=d.x;"
    "document.getElementById('fill').style.width=d.x+'%';});}"
    "function enviar(){let minv=parseInt(min_in.value),maxv=parseInt(max_in.value);"
    "if(isNaN(minv)||isNaN(maxv)||minv<0||maxv>100||minv>maxv){fb('Os valores devem estar entre 0 e 100, sendo que o valor mínimo não pode ser maior que o valor máximo','error');return;}"
    "fetch('/limites/min/'+minv+'/max/'+maxv).then(r=>r.text()).then(t=>fb(t,'ok'));"
    "min_in.value='';max_in.value='';}"
    "function fb(t,c){let e=document.getElementById('feedback');e.className='';e.classList.add(c);e.innerText=t;e.style.opacity=1;setTimeout(()=>{e.style.opacity=0;},5000);}"
    "setInterval(att,1000);"
    "document.addEventListener('DOMContentLoaded',()=>{"
    "document.getElementById('min_in').addEventListener('keydown',k);"
    "document.getElementById('max_in').addEventListener('keydown',k);"
    "function k(e){if(e.key==='Enter')enviar();}});"
    "</script></head><body>"
    "<div class='j'>"
    "<h1>Sistema de abastecimento</h1>"
    "<div class='card'>Bomba:<span id='bomba'>--</span></div>"
    "<div class='card small'>Nível mínimo: <span id='min'>--</span></div>"
    "<div class='card small'>Nível máximo: <span id='max'>--</span></div>"
    "<div class='card'>Nivel:<span id='nivel'>--</span>"
    "<div class='bar'><div id='fill' class='fill'></div></div></div>"
    "<input id='min_in' type='number' min='0' max='100' placeholder='Min'>"
    "<input id='max_in' type='number' min='0' max='100' placeholder='Max'><br>"
    "<button onclick='enviar()'>Enviar</button><br>"
    "<div id='feedback'></div>"
    "</div></body></html>";

struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    // Rota: /limites/min/{min}/max/{max}
    if (strstr(req, "GET /limites/min/") && strstr(req, "/max/"))
    {
        int min = 0, max = 0;
        if (sscanf(req, "GET /limites/min/%d/max/%d", &min, &max) == 2)
        {
            //------Inicio Calculo diferença minima de 20% entre min e max----------
            // min so pode ficar entre 20 e 80
            if (min < 20)
            {
                min = 20;
            }
            else if (min > 80)
            {
                min = 80;
            }
            // calcula diferença mínima de 20 entre mix e max
            if ((max - min) < 20)
            {
                if (min <= 80 && min >= 20)
                {
                    max = min + 20;
                }
            }
            limite_min = min;
            limite_max = max;

            printf("[DEBUG] Novo limite: min=%d, max=%d\n", limite_min, limite_max);

            const char *txt = "Limites atualizados";
            hs->len = snprintf(hs->response, sizeof(hs->response),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: %d\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "%s",
                               (int)strlen(txt), txt);
        }
        else
        {
            const char *txt = "Erro nos parâmetros";
            hs->len = snprintf(hs->response, sizeof(hs->response),
                               "HTTP/1.1 400 Bad Request\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: %d\r\n"
                               "Connection: close\r\n"
                               "\r\n"
                               "%s",
                               (int)strlen(txt), txt);
        }
    }
    // Rota: /estado (usada pelo JS para atualizar a barra)
    else if (strstr(req, "GET /estado"))
    {
        adc_select_input(2);
        uint16_t x_raw = adc_read();
        uint8_t x = (x_raw <= calibracao_minima) ? 100 : (x_raw >= calibracao_maxima) ? 0
                                                                                      : (uint8_t)(((calibracao_maxima - x_raw) * 100) / (calibracao_maxima - calibracao_minima));

        char json_payload[96]; // Aumentado o tamanho do buffer para caber min e max
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"bomba\":%d,\"x\":%d,\"min\":%d,\"max\":%d}\r\n",
                                gpio_get(Bomba), x, limite_min, limite_max); // <- ordem corrigida

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // Rota padrão: devolve o HTML
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

#include "pico/bootrom.h"
#define BOTAO_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_FALL)
    {
        uint64_t agora = time_us_64();

        if (gpio == BOTAO_A)
        {
            if (agora - ultimo_tempo_a > DEBOUNCE_TIME_US)
            {
                ultimo_tempo_a = agora;
                adc_select_input(2);
                calibracao_minima = adc_read();
            }
        }
        else if (gpio == BOTAO_B)
        {
            if (agora - ultimo_tempo_b > DEBOUNCE_TIME_US)
            {
                ultimo_tempo_b = agora;
                adc_select_input(2);
                calibracao_maxima = adc_read();
            }
        }
        else if (gpio == BOTAO_C)
        {
            if (agora - ultimo_tempo_c > DEBOUNCE_TIME_US)
            {
                ultimo_tempo_c = agora;
                reset_usb_boot(0, 0); // Reinicia no modo BOOT
            }
        }
    }
}

int main()
{

    // Inicialização dos botões como entrada com pull-up
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);

    gpio_init(BOTAO_C);
    gpio_set_dir(BOTAO_C, GPIO_IN);
    gpio_pull_up(BOTAO_C);

    // Habilita interrupções na borda de descida (falling edge)
    gpio_set_irq_enabled(BOTAO_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOTAO_B, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOTAO_C, GPIO_IRQ_EDGE_FALL, true);

    // Registra o callback uma única vez
    gpio_set_irq_callback(&gpio_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);

    stdio_init_all();
    sleep_ms(2000);

    gpio_init(Bomba);
    gpio_set_dir(Bomba, GPIO_OUT);

    gpio_init(LED_BOMBA_LIGADA);
    gpio_set_dir(LED_BOMBA_LIGADA, GPIO_OUT);
    gpio_put(LED_BOMBA_LIGADA, false);

    gpio_init(LED_BOMBA_DESLIGADA);
    gpio_set_dir(LED_BOMBA_DESLIGADA, GPIO_OUT);
    gpio_put(LED_BOMBA_DESLIGADA, false);

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    adc_init();
    adc_gpio_init(PIN_BOMBA);

    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();
    char str_x[5]; // Buffer para armazenar a string
    char str_y[5]; // Buffer para armazenar a string
    bool cor = true;

    tocar_start();

    while (true)
    {
        cyw43_arch_poll();

        adc_select_input(2);
        uint16_t posicao_boia = adc_read();

        // Cálculo do nível em porcentagem (com proteção contra divisão por zero)
        uint16_t nivel_de_agua = 0;
        if (calibracao_maxima != calibracao_minima)
        {
            nivel_de_agua = (uint16_t)((1.0f - ((float)(posicao_boia - calibracao_minima) /
                                                (calibracao_maxima - calibracao_minima))) *
                                       100.0f);
        }

        // Estado da bomba
        const char *estado_bomba = gpio_get(Bomba) ? "ON" : "OFF";

        // Buffers de texto
        char str_ip[30];
        char str_nivel[20];
        char str_limites[30];
        char str_estado[20];

        snprintf(str_ip, sizeof(str_ip), "%s", ip_str);
        snprintf(str_nivel, sizeof(str_nivel), "Nivel:%u%%", nivel_de_agua);
        snprintf(str_limites, sizeof(str_limites), "Min:%u Max:%u", limite_min, limite_max);
        snprintf(str_estado, sizeof(str_estado), "Bomba:%s", estado_bomba);

        // Desenha display
        ssd1306_fill(&ssd, !cor);                     // Limpa
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor); // Borda externa

        // Texto
        ssd1306_draw_string(&ssd, str_ip, 6, 6); // Linha 1
        ssd1306_line(&ssd, 4, 16, 124, 16, cor); // Separador

        ssd1306_draw_string(&ssd, str_nivel, 6, 18); // Linha 2
        ssd1306_line(&ssd, 4, 28, 124, 28, cor);     // Separador

        ssd1306_draw_string(&ssd, str_limites, 6, 30); // Linha 3
        ssd1306_line(&ssd, 4, 40, 124, 40, cor);       // Separador

        ssd1306_draw_string(&ssd, str_estado, 6, 42); // Linha 4
        ssd1306_line(&ssd, 4, 52, 124, 52, cor);      // Separador final (opcional)

        ssd1306_send_data(&ssd); // Atualiza display

        printf("Calibracao minima %d calibracao máxima %d \n", calibracao_minima, calibracao_maxima);
        LigDes_bomba();
        sleep_ms(1000);
    }

    cyw43_arch_deinit();
    return 0;
}
