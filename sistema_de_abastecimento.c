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
#define Bomba 13//simula bomba
uint16_t Min_G =20;//guarda nivel min globalmente
uint16_t Max_G =80;//guarda nivel max globalmente
void LigDes_bomba(){
    //------Início Tratamento para simular boia com JOY--------------
        adc_select_input(0);
        uint16_t x = adc_read();
        if(x<400){
            x=400;
        }else if(x>2050){
            x=2050;
        }
        uint16_t nivel=((x-400)/1650.00)*100; //normalziando
        printf("nivel %d\n", nivel);
        //------Fim Tratamento para simular boia com JOY-----------------

        //---------Inicio Func Liga/desliga Bomba------------------------
         
        if(nivel<=Min_G){
            gpio_put(Bomba,1);//simula acionamento da bomba
        }
        if(nivel>=Max_G){
            gpio_put(Bomba,0);//simula desligamento da bomba
        }
        //---------Fim Func Liga/desliga Bomba------------------------
}

#define WIFI_SSID "NomeDaRede"
#define WIFI_PASS "Senha_Do_Wifi"

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C

// Variáveis globais para armazenar min e max
int limite_min = 0;
int limite_max = 4095;

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Abastecimento</title><style>"
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
    "document.getElementById('fill').style.width=(d.x/4095*100)+'%';});}"
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
    "<h1>Abastecimento</h1>"
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
        //min so pode ficar entre 20 e 80
        if(min<20){
            min=20;
        }else if(min>80){
            min=80;
        }
        //calcula diferença mínima de 20 entre mix e max
        if((max-min)<20){
            if(min<=80 && min>=20){
                max=min+20;
            }   
        }
            Max_G= max;//guarda globalmente
            Min_G= min;//guarda globalmente
        //------Fim Calculo diferença minima de 20% entre min e max----------
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

    gpio_init(Bomba);
    gpio_set_dir(Bomba, GPIO_OUT);

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
        LigDes_bomba();
    }

    cyw43_arch_deinit();
    return 0;
}
