#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"

#define LED_PIN 12
#define BOTAO_A 5
#define BOTAO_JOY 22
#define JOYSTICK_X 26
#define JOYSTICK_Y 27

#define WIFI_SSID "BORGES"
#define WIFI_PASS "gomugomu"

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>LED</title><style>"
    "body{font-family:sans-serif;text-align:center;padding:10px;margin:0;background:#f9f9f9}"
    ".b{font-size:20px;padding:10px 30px;margin:10px;border:none;border-radius:8px;cursor:pointer;}"
    ".on{background:#4CAF50;color:#fff}"
    ".br{width:80%;background:#cce6f4;border-radius:6px;overflow:hidden;margin:0 auto 15px;height:40px}"
    ".p{height:100%;transition:width .3s}.x{background:#0077be}"
    ".l{font-weight:700;margin-bottom:5px;display:block}"
    ".j{background:#00A1D7;padding:10px 15px;border-radius:10px;margin:15px auto;font-weight:700;display:block;max-width:60vw;width:90%}"
    "input{padding:10px;margin:5px;border-radius:6px;border:1px solid #ccc;font-size:16px;width:80px}"
    "#feedback{margin-top:10px;font-size:14px;color:#333;transition:opacity 0.5s;}"
    "</style><script>"
    "function u(){fetch('/estado').then(r=>r.json()).then(d=>{"
    "document.getElementById('estado').innerText=d.bomba?'Ligado':'Desligado';"
    "document.getElementById('x_valor').innerText=d.x;"
    "document.getElementById('bx').style.width=Math.round(d.x/4095*100)+'%';"
    "document.getElementById('min_valor').innerText=d.min;"
    "document.getElementById('max_valor').innerText=d.max;"
    "})}"
    "function enviarLimites(){"
    "let min=parseInt(document.getElementById('min').value);"
    "let max=parseInt(document.getElementById('max').value);"
    "const fb=document.getElementById('feedback');"
    "if(isNaN(min)||isNaN(max)||min<0||max>100||min>max){"
    "fb.innerText='Valores inválidos (0-100, min<=max)';fb.style.opacity=1;"
    "setTimeout(()=>fb.style.opacity=0,3000);return;}"
    "fetch(`/limites/min/${min}/max/${max}`)"
    ".then(r=>r.text()).then(data=>{"
    "fb.innerText=data;fb.style.opacity=1;"
    "setTimeout(()=>fb.style.opacity=0,3000);"
    "document.getElementById('min').value='';"
    "document.getElementById('max').value='';"
    "}).catch(err=>{"
    "fb.innerText='Erro: '+err;fb.style.opacity=1;"
    "setTimeout(()=>fb.style.opacity=0,3000);"
    "});"
    "}"
    "document.addEventListener('keydown',e=>{if(e.key==='Enter')enviarLimites();});"
    "setInterval(u,1000);"
    "</script></head><body>"
    "<div class='j'>"
    "<h1>Sistema de abastecimento</h1>"
    "<p>Bomba: <span id='estado'>--</span></p>"
    "<p class='l'>Mínimo: <span id='min_valor'>--</span></p>"
    "<p class='l'>Máximo: <span id='max_valor'>--</span></p>"
    "<p class='l'>Nível da caixa d'água: <span id='x_valor'>--</span></p>"
    "<div class='br'><div id='bx' class='p x'></div></div>"
    "<input type='number' id='min' placeholder='min' min='0' max='100' step='1'>"
    "<input type='number' id='max' placeholder='max' min='0' max='100' step='1'>"
    "<br><button class='b on' onclick='enviarLimites()'>Enviar</button>"
    "<p id='feedback' style='opacity:0;'></p>"
    "</div>";

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

// Variáveis globais para armazenar min e max
int limite_min = 0;
int limite_max = 4095;

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
        adc_select_input(0);
        uint16_t x = adc_read();

        char json_payload[96]; // Aumentado o tamanho do buffer para caber min e max
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"bomba\":%d,\"x\":%d,\"min\":%d,\"max\":%d}\r\n",
                                gpio_get(LED_PIN), x, limite_min, limite_max); // <- ordem corrigida

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
    reset_usb_boot(0, 0);
}

int main()
{
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    sleep_ms(2000);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, true);
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    gpio_init(BOTAO_JOY);
    gpio_set_dir(BOTAO_JOY, GPIO_IN);
    gpio_pull_up(BOTAO_JOY);

    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);

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
    while (true)
    {
        cyw43_arch_poll();

        // Leitura dos valores analógicos
        adc_select_input(0);
        uint16_t adc_value_x = adc_read();
        adc_select_input(1);
        uint16_t adc_value_y = adc_read();

        sprintf(str_x, "%d", adc_value_x);            // Converte o inteiro em string
        sprintf(str_y, "%d", adc_value_y);            // Converte o inteiro em string
        ssd1306_fill(&ssd, !cor);                     // Limpa o display
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor); // Desenha um retângulo
        ssd1306_line(&ssd, 3, 25, 123, 25, cor);      // Desenha uma linha
        ssd1306_line(&ssd, 3, 37, 123, 37, cor);      // Desenha uma linha

        ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6); // Desenha uma string
        ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);  // Desenha uma string
        ssd1306_draw_string(&ssd, ip_str, 10, 28);
        ssd1306_draw_string(&ssd, "X    Y    PB", 20, 41);           // Desenha uma string
        ssd1306_line(&ssd, 44, 37, 44, 60, cor);                     // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, str_x, 8, 52);                     // Desenha uma string
        ssd1306_line(&ssd, 84, 37, 84, 60, cor);                     // Desenha uma linha vertical
        ssd1306_draw_string(&ssd, str_y, 49, 52);                    // Desenha uma string
        ssd1306_rect(&ssd, 52, 90, 8, 8, cor, !gpio_get(BOTAO_JOY)); // Desenha um retângulo
        ssd1306_rect(&ssd, 52, 102, 8, 8, cor, !gpio_get(BOTAO_A));  // Desenha um retângulo
        ssd1306_rect(&ssd, 52, 114, 8, 8, cor, !cor);                // Desenha um retângulo
        ssd1306_send_data(&ssd);                                     // Atualiza o display

        sleep_ms(300);
    }

    cyw43_arch_deinit();
    return 0;
}
