#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "main";
#define V_REF 1100
#define ADC1_TEST_CHANNEL (ADC1_CHANNEL_7)

// i2s number
#define EXAMPLE_I2S_NUM (0)
// i2s sample rate
#define EXAMPLE_I2S_SAMPLE_RATE (44100)
// i2s data bits
#define EXAMPLE_I2S_SAMPLE_BITS (16)
// enable display buffer for debug
#define EXAMPLE_I2S_BUF_DEBUG (1)
// I2S read buffer length
#define EXAMPLE_I2S_READ_LEN (16 * 1024)
// I2S data format
#define EXAMPLE_I2S_FORMAT (I2S_CHANNEL_FMT_RIGHT_LEFT)
// I2S channel number
#define EXAMPLE_I2S_CHANNEL_NUM ((EXAMPLE_I2S_FORMAT < I2S_CHANNEL_FMT_ONLY_RIGHT) ? (2) : (1))
// I2S built-in ADC unit
#define I2S_ADC_UNIT ADC_UNIT_1
// I2S built-in ADC channel
#define I2S_ADC_CHANNEL ADC1_CHANNEL_0

/**
 * @brief I2S ADC/DAC mode init.
 */
void example_i2s_init(void)
{
    int i2s_num = EXAMPLE_I2S_NUM;
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = EXAMPLE_I2S_SAMPLE_RATE,
        .bits_per_sample = EXAMPLE_I2S_SAMPLE_BITS,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .channel_format = EXAMPLE_I2S_FORMAT,
        .intr_alloc_flags = 0,
        // .dma_desc_num = 2,
        // deprecated
        .dma_buf_count = 2,
        //.dma_frame_num = 1024,
        // deprecated
        .dma_buf_len = 1024,
        .use_apll = 1,
    };
    // install and start i2s driver
    i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
    // init DAC pad
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    // init ADC pad
    i2s_set_adc_mode(I2S_ADC_UNIT, I2S_ADC_CHANNEL);
}

/**
 * @brief debug buffer data
 */
void example_disp_buf(uint8_t *buf, int length)
{
#if EXAMPLE_I2S_BUF_DEBUG
    printf("======\n");
    for (int i = 0; i < length; i++)
    {
        printf("%02x ", buf[i]);
        if ((i + 1) % 8 == 0)
        {
            printf("\n");
        }
    }
    printf("======\n");
#endif
}

/**
 * @brief Scale data to 16bit/32bit for I2S DMA output.
 *        DAC can only output 8bit data value.
 *        I2S DMA will still send 16 bit or 32bit data, the highest 8bit contains DAC data.
 */
int example_i2s_dac_data_scale(uint8_t *d_buff, uint8_t *s_buff, uint32_t len)
{
    uint32_t j = 0;
#if (EXAMPLE_I2S_SAMPLE_BITS == 16)
    for (int i = 0; i < len; i++)
    {
        d_buff[j++] = 0;
        d_buff[j++] = s_buff[i];
    }
    return (len * 2);
#else
    for (int i = 0; i < len; i++)
    {
        d_buff[j++] = 0;
        d_buff[j++] = 0;
        d_buff[j++] = 0;
        d_buff[j++] = s_buff[i];
    }
    return (len * 4);
#endif
}

/**
 * @brief Scale data to 8bit for data from ADC.
 *        Data from ADC are 12bit width by default.
 *        DAC can only output 8 bit data.
 *        Scale each 12bit ADC data to 8bit DAC data.
 */
void example_i2s_adc_data_scale(uint8_t *d_buff, uint8_t *s_buff, uint32_t len)
{
    uint32_t j = 0;
    uint32_t dac_value = 0;
#if (EXAMPLE_I2S_SAMPLE_BITS == 16)
    for (int i = 0; i < len; i += 2)
    {
        dac_value = ((((uint16_t)(s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 4096;
    }
#else
    for (int i = 0; i < len; i += 4)
    {
        dac_value = ((((uint16_t)(s_buff[i + 3] & 0xf) << 8) | ((s_buff[i + 2]))));
        d_buff[j++] = 0;
        d_buff[j++] = 0;
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 4096;
    }
#endif
}

/**
 * @brief I2S ADC/DAC example
 *        1. Read audio from ADC and store in buffer
 *        2. Replay the sound in buffer via DAC
 */
void example_i2s_adc_dac(void *arg)
{
    int i2s_read_len = EXAMPLE_I2S_READ_LEN;
    size_t bytes_read, bytes_written;

    uint8_t *i2s_read_buff = (uint8_t *)calloc(i2s_read_len, sizeof(char));
    i2s_adc_enable(EXAMPLE_I2S_NUM);
    uint8_t *i2s_write_buff = (uint8_t *)calloc(i2s_read_len, sizeof(char));

    while (1)
    {
        // read data from I2S bus, in this case, from ADC.
        i2s_read(EXAMPLE_I2S_NUM, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
        example_disp_buf(i2s_read_buff, 64);

        // i2s_adc_disable(EXAMPLE_I2S_NUM);
        // free(i2s_read_buff);
        // i2s_read_buff = NULL;

        // process data and scale to 8bit for I2S DAC.
        example_i2s_adc_data_scale(i2s_write_buff, i2s_read_buff, i2s_read_len);
        // send data
        i2s_write(EXAMPLE_I2S_NUM, i2s_write_buff, i2s_read_len, &bytes_written, portMAX_DELAY);
    }

    free(i2s_write_buff);

    vTaskDelete(NULL);
}

void adc_read_task(void *arg)
{
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_TEST_CHANNEL, ADC_ATTEN_11db);
    esp_adc_cal_characteristics_t characteristics;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, V_REF, &characteristics);
    while (1)
    {
        uint32_t voltage;
        esp_adc_cal_get_voltage(ADC1_TEST_CHANNEL, &characteristics, &voltage);
        ESP_LOGI(TAG, "%d mV", voltage);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

esp_err_t app_main(void)
{
    example_i2s_init();
    esp_log_level_set("I2S", ESP_LOG_INFO);
    xTaskCreate(example_i2s_adc_dac, "example_i2s_adc_dac", 1024 * 2, NULL, 5, NULL);
    // xTaskCreate(adc_read_task, "ADC read task", 2048, NULL, 5, NULL);
    return ESP_OK;
}