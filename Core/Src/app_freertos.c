/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_freertos.h"
#include "app_printer.h"
#include "escpos_commands.h"
#include "myprintf.h"
#include "print_buffer.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Definitions for defaultTask
 * 当前只作为占位任务保留，不再承担业务主链。
 */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityLow,
  .stack_size = 512
};

/* 接收处理任务 */
osThreadId_t rxProcessTaskHandle;
const osThreadAttr_t rxProcessTask_attributes = {
  .name = "rxProcessTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 1024
};

/* 打印处理任务
 * dotmatrix + UART2 输出链对栈需求明显高于接收链，
 * 当前版本实测 4096 字节可稳定运行。
 */
osThreadId_t printTaskHandle;
const osThreadAttr_t printTask_attributes = {
  .name = "printTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 4096
};

/* UART1 日志发送互斥锁 */
osMutexId_t uart1LogMutexHandle;
const osMutexAttr_t uart1LogMutex_attributes = {
  .name = "uart1LogMutex"
};

/* 新增：打印请求计数信号量 */
osSemaphoreId_t printRequestSemaphoreHandle;
const osSemaphoreAttr_t printRequestSemaphore_attributes = {
  .name = "printRequestSemaphore"
};

/* 新增：print_buffer 互斥锁 */
osMutexId_t printBufferMutexHandle;
const osMutexAttr_t printBufferMutex_attributes = {
  .name = "printBufferMutex"
};

/* 新增：settings 互斥锁 */
osMutexId_t settingsMutexHandle;
const osMutexAttr_t settingsMutex_attributes = {
  .name = "settingsMutex"
};


/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartRxProcessTask(void *argument);
static void StartPrintTask(void *argument);
/* USER CODE END FunctionPrototypes */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 先创建 UART1 日志 mutex */
  uart1LogMutexHandle = osMutexNew(&uart1LogMutex_attributes);
  if (uart1LogMutexHandle == NULL)
  {
    Error_Handler();
  }

  log_bind_uart1_mutex(uart1LogMutexHandle);




  /* 新增：创建 print_buffer mutex */
  printBufferMutexHandle = osMutexNew(&printBufferMutex_attributes);
  if (printBufferMutexHandle == NULL)
  {
    Error_Handler();
  }

  print_buffer_bind_mutex(printBufferMutexHandle);

  settingsMutexHandle = osMutexNew(&settingsMutex_attributes);
  if (settingsMutexHandle == NULL)
  {
    Error_Handler();
  }

  printer_bind_settings_mutex(settingsMutexHandle);


  printRequestSemaphoreHandle = osSemaphoreNew(32, 0, &printRequestSemaphore_attributes);
  if (printRequestSemaphoreHandle == NULL)
  {
    Error_Handler();
  }

  printer_bind_print_semaphore(printRequestSemaphoreHandle);


  /* USER CODE BEGIN RTOS_MUTEX */
  /* add other mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* 保留 defaultTask，但不再承担业务主链 */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* 创建两个业务任务 */
  rxProcessTaskHandle = osThreadNew(StartRxProcessTask, NULL, &rxProcessTask_attributes);
  printTaskHandle     = osThreadNew(StartPrintTask, NULL, &printTask_attributes);

  /* 绑定打印任务句柄，供命令层发通知 */
  printer_bind_print_task(printTaskHandle);

  /* 基础任务创建检查 */
  if (defaultTaskHandle == NULL || rxProcessTaskHandle == NULL || printTaskHandle == NULL)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* 占位任务：当前不承担业务逻辑 */
void StartDefaultTask(void *argument)
{
  for(;;)
  {
    osDelay(1000);
  }
}

/* 接收处理任务 */
static void StartRxProcessTask(void *argument)
{
  for(;;)
  {
    app_printer_process_rx();
    osDelay(5);
  }
}

/* 打印处理任务
 * 当前改为阻塞等待通知，不再轮询 osDelay(5)
 */
static void StartPrintTask(void *argument)
{
  for(;;)
  {
    app_printer_wait_print_task();
  }
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
