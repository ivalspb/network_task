# Network Monitoring System

Рефакотринг многоуровневой системы мониторинга сетевых метрик и состояния устройств с сервер-клиент архитектурой.

## 🏗️ Архитектура проекта

Проект использует компонентный подход с четким разделением ответственности и соблюдением принципов SOLID.

```
monitoring-system/
├── common/                          # Общие компоненты
│   ├── include/
│   │   ├── protocol/               # Протокол сообщений
│   │   │   ├── message.h
│   │   │   ├── serializer.h
│   │   │   └── message_factory.h
│   │   ├── utils/                  # Утилиты
│   │   │   ├── logger.h
│   │   │   ├── timestamp.h
│   │   │   └── validator.h
│   │   └── interfaces/             # Базовые интерфейсы
│   │       ├── iconnectable.h
│   │       ├── imetric_provider.h
│   │       └── iconfigurable.h
│   └── src/
│       └── ... (реализации)
├── server/                         # Серверная часть
│   ├── include/
│   │   ├── core/                  # Ядро сервера
│   │   │   ├── server.h
│   │   │   ├── server_worker.h
│   │   │   └── server_factory.h
│   │   ├── network/               # Сетевой слой
│   │   │   ├── tcp_server.h
│   │   │   ├── connection_pool.h
│   │   │   └── client_socket.h
│   │   ├── session/               # Управление сессиями
│   │   │   ├── client_session.h
│   │   │   └── session_manager.h
│   │   ├── processing/            # Обработка сообщений
│   │   │   ├── message_processor.h
│   │   │   └── threshold_manager.h
│   │   ├── monitoring/            # Мониторинг
│   │   │   ├── health_monitor.h
│   │   │   └── activity_tracker.h
│   │   └── GUI/                   # Графический интерфейс
│   │       ├── view/
│   │       │   ├── main_window.h
│   │       │   ├── clients_table.h
│   │       │   └── metrics_display.h
│   │       ├── presenter/
│   │       │   ├── server_presenter.h
│   │       │   └── client_presenter.h
│   │       ├── model/
│   │       │   ├── server_model.h
│   │       │   └── client_model.h
│   │       └── widgets/
│   │           └── settings_dialog.h
│   └── src/
│       └── ... (реализации)
├── client/                         # Клиентская часть
│   ├── include/
│   │   ├── core/                  # Ядро клиента
│   │   │   ├── client.h
│   │   │   └── client_controller.h
│   │   ├── connection/            # Подключение
│   │   │   ├── connection_manager.h
│   │   │   └── reconnect_strategy.h
│   │   ├── metrics/               # Сбор метрик
│   │   │   ├── metrics_collector.h
│   │   │   ├── network/
│   │   │   │   ├── bandwidth_monitor.h
│   │   │   │   ├── latency_monitor.h
│   │   │   │   └── packet_loss_monitor.h
│   │   │   └── system/
│   │   │       ├── cpu_monitor.h
│   │   │       ├── memory_monitor.h
│   │   │       └── uptime_monitor.h
│   │   ├── services/              # Сервисы
│   │   │   ├── heartbeat_service.h
│   │   │   ├── data_service.h
│   │   │   └── log_service.h
│   │   └── utils/                 # Утилиты
│   │       ├── config_loader.h
│   │       └── platform_detector.h
│   └── src/
│       └── ... (реализации)
└── tests/                          # Тесты
    ├── unit/                      # Unit-тесты
    ├── integration/               # Интеграционные тесты
    └── mocks/                     # Mock-объекты
```

## 🎯 Основные компоненты

### Серверная часть
- **Сетевой слой**: Управление TCP соединениями, буферизация данных
- **Менеджер сессий**: Управление клиентскими сессиями и состоянием
- **Обработчик сообщений**: Парсинг и валидация входящих сообщений
- **Мониторинг**: Проверка активности клиентов и пороговых значений
- **GUI**: Современный интерфейс на основе паттерна MVP

### Клиентская часть
- **Сбор метрик**: Мониторинг сети, CPU, памяти, uptime
- **Управление подключением**: Автоматическое переподключение
- **Сервисы**: Heartbeat, отправка данных, логирование
- **Платформо-независимость**: Поддержка Linux/Windows

### Общие компоненты
- **Протокол**: JSON-based messaging с фреймированием
- **Утилиты**: Логирование, валидация, работа со временем
- **Интерфейсы**: Базовые контракты для слабой связности

## 📋 Функциональность

### ✅ Реализовано
- [x] TCP сервер с управлением подключениями
- [x] Клиенты с автоматическим переподключением
- [x] Сбор сетевых метрик (bandwidth, latency, packet loss)
- [x] Мониторинг системы (CPU, memory, uptime)
- [x] Генерация реалистичных логов
- [x] Проверка пороговых значений
- [x] Графический интерфейс сервера
- [x] Heartbeat мониторинг
- [x] Буферизация и фреймирование сообщений

### 🔧 Технические особенности
- **Протокол**: JSON over TCP с `\n` разделителем
- **Порты**: 1024-65535 (по умолчанию 12345)
- **Кодировка**: UTF-8
- **Таймауты**:
  - Подключение: 2 секунды
  - Heartbeat: 15 секунд
  - Неактивность: 30 секунд

## 🚀 Быстрый старт

### Сборка проекта

```bash
# Клонирование репозитория
git clone <repository-url>
cd monitoring-system

# Создание build директории
mkdir build && cd build

# Генерация проекта (Linux)
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
make -j4

# Или с использованием Conan (опционально)
conan install ..
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Запуск сервера

```bash
# Запуск серверного приложения
./bin/server_app

# Или с указанием порта
./bin/server_app --port 8080
```

### Запуск клиента

```bash
# Подключение к localhost
./bin/client_app

# Подключение к конкретному серверу
./bin/client_app --address 192.168.1.100 --port 8080

# Показать help
./bin/client_app --help
```

## 🧪 Тестирование

### Запуск тестов

```bash
# Все тесты
./bin/server_tests

# Конкретная группа тестов
./bin/server_tests --gtest_filter="Network*"
./bin/server_tests --gtest_filter="*ServerWorker*"

# С кодом покрытия
gcovr -r . --html --html-details -o coverage.html
```

### Типы тестов

- **Unit тесты**: Тестирование отдельных компонентов
- **Интеграционные тесты**: Тестирование взаимодействия компонентов
- **Mock тесты**: Тестирование с mock-объектами

## ⚙️ Конфигурация

### Формат конфигурации

```json
{
  "server": {
    "port": 12345,
    "max_connections": 100,
    "heartbeat_interval": 10000
  },
  "thresholds": {
    "cpu": 80.0,
    "memory": 85.0,
    "bandwidth": 800.0,
    "latency": 100.0
  },
  "logging": {
    "level": "info",
    "file": "server.log",
    "max_size": 10485760
  }
}
```

### Переменные окружения

```bash
# Порт сервера
export MONITORING_PORT=8080

# Уровень логирования
export LOG_LEVEL=debug

# Файл конфигурации
export CONFIG_FILE=config/production.json
```

## 📊 Протокол сообщений

### Формат сообщения

```json
{
  "type": "MessageType",
  "client_id": 123,
  "process_id": "pid_12345",
  "timestamp": "2024-01-15T10:30:00.000Z",
  "data": {}
}
```

### Типы сообщений

| Тип | Направление | Описание |
|-----|-------------|----------|
| `ClientHandshake` | Client→Server | Первоначальное подключение |
| `ConnectionAck` | Server→Client | Подтверждение подключения |
| `NetworkMetrics` | Client→Server | Сетевые метрики |
| `DeviceStatus` | Client→Server | Статус устройства |
| `Heartbeat` | Client→Server | Проверка активности |
| `HeartbeatResponse` | Server→Client | Ответ на heartbeat |
| `ThresholdWarning` | Server→Client | Превышение порога |

## 🛠️ Разработка

### Стиль кода

- **Именование**: CamelCase для классов, snake_case для переменных
- **Отступы**: 4 пробела
- **Комментарии**: Doxygen style
- **Линтеры**: clang-format, cppcheck

### Правила коммитов

```
feat: добавление новой функциональности
fix: исправление ошибки
docs: обновление документации
style: изменения форматирования
refactor: рефакторинг кода
test: добавление тестов
chore: вспомогательные изменения
```

### Процесс разработки

1. Создать feature branch от `develop`
2. Реализовать функциональность с тестами
3. Пройти code review
4. Замержить в `develop`
5. Выпустить релиз из `develop` в `main`

## 📈 Производительность

### Бенчмарки

| Компонент | Метрика | Значение |
|-----------|---------|----------|
| Сервер | Подключений/сек | 1000+ |
| Сервер | Сообщений/сек | 5000+ |
| Клиент | Память | < 50MB |
| Клиент | CPU | < 5% |

### Оптимизации

- **Пулы соединений**: Переиспользование сокетов
- **Буферизация**: Batch обработка сообщений
- **Асинхронность**: Non-blocking I/O операции
- **Кэширование**: Кэш метрик и состояний

## 🔮 Roadmap

### Ближайшие задачи
- [ ] WebSocket поддержка
- [ ] REST API для управления
- [ ] База данных для хранения истории
- [ ] Docker контейнеризация
- [ ] Prometheus экспортер

### Будущие возможности
- [ ] Кластерная конфигурация серверов
- [ ] Мобильное приложение для мониторинга
- [ ] Machine learning для предсказания аномалий
- [ ] Plugin system для метрик

## 🤝 Contributing

1. Форкните репозиторий
2. Создайте feature branch (`git checkout -b feature/amazing-feature`)
3. Закоммитьте изменения (`git commit -m 'Add amazing feature'`)
4. Запушьте branch (`git push origin feature/amazing-feature`)
5. Откройте Pull Request

## 📄 Лицензия

Проект лицензирован под MIT License - смотрите файл [LICENSE](LICENSE) для деталей.

## 🆘 Поддержка

- **Документация**: [docs/](docs/)
- **Баг-репорты**: [GitHub Issues](https://github.com/ivalspb/network_task/issues)
- **Вопросы**: [Discussions](https://github.com/ivalspb/network_task/discussions)
- **Email**: ival.spb@gmail.com

---

**Примечание**: Этот проект находится в активной разработке. API может изменяться между версиями.
