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
volatile uint16_t ld_range_cm = 0;   /* last reported range */
volatile uint32_t ld_last_rx = 0;    /* HAL tick of last valid line */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

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

    char msg[128];
    snprintf(msg, sizeof(msg),
        "RAW: M=%u V=%u | RMS: %.2f | VIB: %.2f | RADAR: %s R=%u\r\n",
        adc_vals[0], adc_vals[1], mic_rms, vib_avg,
        ld_presence ? "ON " : "OFF", ld_range_cm);

    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

if(HAL_GetTick() - window_start >= 5000)  // 5 sec window
    {
        window_start = HAL_GetTick();

        mic_acc = 0;
        vib_acc = 0;
        sample_count = 0;
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
        ld_last_rx = HAL_GetTick();
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
