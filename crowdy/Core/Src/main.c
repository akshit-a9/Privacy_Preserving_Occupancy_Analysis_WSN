/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t adc_vals[2];

uint64_t mic_acc = 0;
uint64_t vib_acc = 0;

uint32_t sample_count = 0;

uint32_t last_print = 0;
uint32_t window_start = 0;

/* LD2420 line-assembly + parsed state */
#define LD_LINE_MAX 32
static uint8_t  ld_rx_byte;
static char     ld_line[LD_LINE_MAX];
static uint8_t  ld_line_len = 0;
volatile uint8_t  ld_presence = 0;   /* 1 = ON, 0 = OFF */
volatile uint16_t ld_range_cm = 0;   /* last reported range (for display) */
volatile uint32_t ld_last_rx = 0;    /* HAL tick of last valid line */
volatile uint32_t ld_range_sum = 0;  /* sum of range samples in current window */
volatile uint32_t ld_range_n   = 0;  /* count of range samples in current window */

/* ---- Occupancy thresholds (TUNE IN LAB) -------------------------------
   Mic & vibration: 0=EMPTY,1=LOW,2=MED,3=HIGH based on plain thresholds.
     x < LOW_THR          -> EMPTY
     LOW_THR <= x < MED   -> LOW
     MED_THR <= x < HIGH  -> MEDIUM
     x >= HIGH_THR        -> HIGH
   Measured single-occupant values: mic RMS ~1615, vib ~122. */
#define MIC_LOW_THR     1700.0f
#define MIC_MED_THR     1800.0f
#define MIC_HIGH_THR    1900.0f

#define VIB_LOW_THR     18.0f
#define VIB_MED_THR     25.0f
#define VIB_HIGH_THR    30.0f
#define VIB_INVALID_THR 2000.0f    /* reading above this => sensor unplugged, ignore */

#define MIC_INVALID_THR 500.0f     /* RMS below this => mic unplugged, ignore */

/* Radar uses inverted thresholds: closer range => more crowded.
     presence OFF, no samples, OR range > EMPTY_THR -> EMPTY
     LOW_THR  < range <= EMPTY_THR                  -> LOW
     HIGH_THR < range <= LOW_THR                    -> MEDIUM
     range <= HIGH_THR                              -> HIGH  */
#define RADAR_EMPTY_THR 200       /* range in cm */
#define RADAR_LOW_THR   50
#define RADAR_HIGH_THR  20

/* Fusion weights (need not sum to 1; combined level is the rounded
   weighted mean of the per-sensor levels). */
#define W_MIC   1.0f
#define W_VIB   1.0f
#define W_RADAR 1.5f
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t classify(float x, float t_low, float t_med, float t_high);
static const char* level_name(uint8_t lvl);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
MX_USART1_UART_Init();
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_vals, 2);
HAL_UART_Receive_IT(&huart1, &ld_rx_byte, 1);

window_start = HAL_GetTick();
last_print = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		if(HAL_GetTick() - last_print >= 100)
{
    last_print = HAL_GetTick();

    float mic_rms = 0;
    float vib_avg = 0;

    if(sample_count > 0)
    {
        mic_rms = sqrt((float)mic_acc / sample_count);

        // invert vibration (adjust baseline experimentally)
        float baseline = 4095.0f;
        vib_avg = baseline - ((float)vib_acc / sample_count);
    }

    /* windowed average radar range (cm), 0 if no samples yet */
    uint16_t range_avg = (ld_range_n > 0)
                         ? (uint16_t)(ld_range_sum / ld_range_n)
                         : 0u;

    uint8_t mic_valid = (mic_rms >= MIC_INVALID_THR);
    uint8_t vib_valid = (vib_avg <= VIB_INVALID_THR);
    uint8_t rad_valid = (range_avg > 0);

    uint8_t lvl_mic = mic_valid
                      ? classify(mic_rms, MIC_LOW_THR, MIC_MED_THR, MIC_HIGH_THR)
                      : 0;
    uint8_t lvl_vib = vib_valid
                      ? classify(vib_avg, VIB_LOW_THR, VIB_MED_THR, VIB_HIGH_THR)
                      : 0;

    /* Radar: inverted — closer => more crowded. Presence OFF or range beyond
       EMPTY_THR short-circuit to EMPTY. range_avg==0 => sensor disconnected. */
    uint8_t lvl_rad;
    if(!rad_valid)                           lvl_rad = 0;
    else if(!ld_presence)                    lvl_rad = 0;
    else if(range_avg >  RADAR_EMPTY_THR)    lvl_rad = 0;
    else if(range_avg >  RADAR_LOW_THR)      lvl_rad = 1;
    else if(range_avg >  RADAR_HIGH_THR)     lvl_rad = 2;
    else                                     lvl_rad = 3;

    uint8_t disc_count = (!mic_valid) + (!vib_valid) + (!rad_valid);

    char msg[220];
    if(disc_count == 3) {
        snprintf(msg, sizeof(msg), "All sensors disconnected\r\n");
    } else {
        float w_mic_eff = mic_valid ? W_MIC   : 0.0f;
        float w_vib_eff = vib_valid ? W_VIB   : 0.0f;
        float w_rad_eff = rad_valid ? W_RADAR : 0.0f;
        float w_sum     = w_mic_eff + w_vib_eff + w_rad_eff;
        float lvl_num   = (w_mic_eff * lvl_mic + w_vib_eff * lvl_vib + w_rad_eff * lvl_rad) / w_sum;
        uint8_t lvl_combined = (uint8_t)(lvl_num + 0.5f);   /* round */
        if(lvl_combined > 3) lvl_combined = 3;

        char mic_lvl_ch = mic_valid ? (char)('0' + lvl_mic) : '-';
        char vib_lvl_ch = vib_valid ? (char)('0' + lvl_vib) : '-';
        char rad_lvl_ch = rad_valid ? (char)('0' + lvl_rad) : '-';

        const char *warn = (disc_count == 2) ? "  [WARNING: 2 sensors disconnected]" : "";

        snprintf(msg, sizeof(msg),
            "MIC raw:%-4u rms:%7.2f | VIB raw:%-4u avg:%6.2f | RADAR %s raw:%-4u avg:%-4u | M:%c V:%c R:%c => %s%s\r\n",
            adc_vals[0], mic_rms,
            adc_vals[1], vib_avg,
            ld_presence ? "ON " : "OFF", ld_range_cm, range_avg,
            mic_lvl_ch, vib_lvl_ch, rad_lvl_ch, level_name(lvl_combined), warn);
    }

    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

if(HAL_GetTick() - window_start >= 5000)  // 5 sec window
    {
        window_start = HAL_GetTick();

        mic_acc = 0;
        vib_acc = 0;
        sample_count = 0;

        ld_range_sum = 0;
        ld_range_n   = 0;
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc->Instance == ADC1)
    {
        uint16_t mic = adc_vals[0];
        uint16_t vib = adc_vals[1];

        // accumulate for processing
        mic_acc += mic * mic;   // RMS
        vib_acc += vib;

        sample_count++;
    }
}

/* Parse a single completed line from the LD2420.
   Expected tokens: "ON", "OFF", "Range <cm>". Unknown lines ignored. */
static void ld_parse_line(const char *s, uint8_t n)
{
    while(n && (s[n-1] == ' ' || s[n-1] == '\t')) n--;
    if(n == 0) return;

    if(n == 2 && s[0] == 'O' && s[1] == 'N') {
        ld_presence = 1;
        ld_last_rx = HAL_GetTick();
    } else if(n == 3 && s[0] == 'O' && s[1] == 'F' && s[2] == 'F') {
        ld_presence = 0;
        ld_last_rx = HAL_GetTick();
    } else if(n > 6 && strncmp(s, "Range ", 6) == 0) {
        uint32_t v = 0;
        for(uint8_t i = 6; i < n; i++) {
            if(s[i] < '0' || s[i] > '9') return;
            v = v * 10 + (s[i] - '0');
            if(v > 65535) return;
        }
        ld_range_cm = (uint16_t)v;
        ld_range_sum += v;
        ld_range_n++;
        ld_last_rx = HAL_GetTick();
    }
}

static uint8_t classify(float x, float t_low, float t_med, float t_high)
{
    if(x < t_low)  return 0;
    if(x < t_med)  return 1;
    if(x < t_high) return 2;
    return 3;
}

static const char* level_name(uint8_t lvl)
{
    switch(lvl) {
        case 0:  return "EMPTY";
        case 1:  return "LOW";
        case 2:  return "MEDIUM";
        default: return "HIGH";
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1) {
        char c = (char)ld_rx_byte;
        if(c == '\r' || c == '\n') {
            if(ld_line_len > 0) {
                ld_parse_line(ld_line, ld_line_len);
                ld_line_len = 0;
            }
        } else if(ld_line_len < LD_LINE_MAX - 1) {
            ld_line[ld_line_len++] = c;
        } else {
            /* overflow — drop the in-progress line */
            ld_line_len = 0;
        }
        HAL_UART_Receive_IT(&huart1, &ld_rx_byte, 1);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
