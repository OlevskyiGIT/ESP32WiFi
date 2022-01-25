/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "driver/uart.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define EXAMPLE_ESP_MAXIMUM_RETRY  50

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define PORT "80"

static const char *TAG = "wifi station";

static int s_retry_num = 0;
char UARTRecBuf[127];
char UARTSendBuf[127];

void myUARTInit(void)
{
	uart_config_t uart_config = {
	        .baud_rate = 115200,
	        .data_bits = UART_DATA_8_BITS,
	        .parity = UART_PARITY_DISABLE,
	        .stop_bits = UART_STOP_BITS_1,
	        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	        .rx_flow_ctrl_thresh = 122,
	        .source_clk = UART_SCLK_APB,
	    };
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 127, 127, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 23, 22, 18, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX));
	ESP_ERROR_CHECK(uart_set_rx_timeout(UART_NUM_1, 3));
}

static inline void myUARTSend(char *toSend)
{
	uart_write_bytes(UART_NUM_1, (const char *)toSend, strlen(toSend));
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
        	vTaskDelay(1000 / portTICK_RATE_MS);
            esp_wifi_connect();
            s_retry_num++;
            myUARTSend("retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        myUARTSend("connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(UARTSendBuf, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        myUARTSend(UARTSendBuf);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(char *mySSID, char *myPass)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strcpy((char *)wifi_config.sta.ssid, mySSID);
    strcpy((char *)wifi_config.sta.password, mySSID);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    myUARTSend("wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
    	sprintf(UARTSendBuf, "connected to ap SSID:%s password:%s", mySSID, myPass);
        myUARTSend(UARTSendBuf);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
    	sprintf(UARTSendBuf, "Failed to connect to SSID:%s, password:%s", mySSID, myPass);
    	myUARTSend(UARTSendBuf);
    }
    else
    {
    	myUARTSend("UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void send_HTTP_req(int GET, char *Site, char *body)
{
	const struct addrinfo hints =
	{
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	struct addrinfo *res;
	int s;
	while(1)
	{
		//Resolve the IP
		int result = getaddrinfo(Site, PORT, &hints, &res);
		if((result != 0) || (res == NULL))
		{
			sprintf(UARTSendBuf, "Unable to resolve IP for target website %s\n", Site);
			myUARTSend(UARTSendBuf);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
		myUARTSend("Target website's IP resolved\n");

		//Allocate socket
		s = socket(res->ai_family, res->ai_socktype, 0);
		if(s < 0)
		{
			myUARTSend("Unable to allocate a new socket\n");
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
		ESP_LOGI(TAG, "Socket allocated, id=%d\n", s);

		//Connect to site
		result = connect(s, res->ai_addr, res->ai_addrlen);
		if(result != 0)
		{
			myUARTSend("Unable to connect to the target website\n");
			close(s);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
		myUARTSend("Connected to the target website\n");

		//Request generation
		char *REQ;
		if(GET)
		{
			REQ = (char *)calloc(strlen("http GET ") + strlen(Site) + 1, sizeof(char));
			strcat(REQ, "http GET ");
			strcat(REQ, Site);
		}
		else
		{
			REQ = (char *)calloc(strlen("http POST ") + strlen(Site) + strlen(body) + 1, sizeof(char));
			strcat(REQ, "http POST ");
			strcat(REQ, Site);
			strcat(REQ, body);
		}
		result = write(s, REQ, strlen(REQ));
		free(REQ);
		if(result < 0)
		{
			myUARTSend("Unable to send the HTTP request\n");
			close(s);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
		myUARTSend("HTTP request sent\n");
		break;
	}

	//Response
	myUARTSend("Response:\r\n");
	int r;
	do
	{
		bzero(UARTSendBuf, sizeof(UARTSendBuf));
		r = read(s, UARTSendBuf, sizeof(UARTSendBuf) - 1);
		myUARTSend(UARTSendBuf);
	}
	while(r > 0);

	close(s);
	myUARTSend("Socket closed\r\n");
}

void workerFun(void *pvParameters)
{
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	char mySSID[] = "SSID";
	char myPass[] = "Password";
	wifi_init_sta(mySSID, myPass);
	myUARTInit();
	int GET = 0;
	char site[127];
	char body[127];
	int len;
	while(1)
	{
		myUARTSend("Please enter the site address:\r\n");
		do
		{
			len = uart_read_bytes(UART_NUM_1, UARTRecBuf, 127, 100 / portTICK_RATE_MS);
		}
		while(len <= 0);
		strcpy(site, UARTRecBuf);
		myUARTSend("Please enter the body (nothing to send a GET):\r\n");
		do
		{
			len = uart_read_bytes(UART_NUM_1, UARTRecBuf, 127, 100 / portTICK_RATE_MS);
		}
		while(len <= 0);
		strcpy(body, UARTRecBuf);
		if(body[0] == '\n') GET = 1;
		send_HTTP_req(GET, site, body);
	}
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(&workerFun, "Main", 4096, NULL, 5, NULL);
}
