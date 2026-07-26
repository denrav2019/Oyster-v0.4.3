# Oyster Programming Language

![Version](https://img.shields.io/badge/version-0.4.3-blue)
![License](https://img.shields.io/badge/license-MIT-green)

* Oyster is a minimalistic, Perl-inspired scripting programming language with a focus on simplicity and performance.
* Oyster — минималистичный скриптовый язык программирования, вдохновленный Perl и ориентированный на простоту и производительность

**Motto / Девиз:** Efficiency, readability, minimalism! / Эффективность, читаемость, минимализм!

## Features / Возможности
- Dynamic typing with 16-byte tagged values / Динамическая типизация с 16-байтными tagged values
- Perl-inspired syntax with modern enhancements / Perl-подобный синтаксис с современными улучшениями
- Fast VM with dispatch table (ASCII → handler) / Быстрая VM с таблицей диспетчеризации
- Module system with .osm (source) and .ocm (compiled) / Модульная система с .osm и .ocm
- Case-insensitive syntax / Регистронезависимый синтаксис
- Method-style function calls / Методные вызовы функций
- Postfix notation support / Поддержка обратной польской записи
- Fixed-point arithmetic (64.32) / Арифметика с фиксированной точкой
- UTF-32 strings with O(1) indexed access / Строки UTF-32 с индексированным доступом O(1)

## Installation / Установка

`git clone https://github.com/denrav2019/Oyster-v0.4.3.git`  
`cd Oyster-v0.4.3`  
`make`  
`sudo make install`

## Quick Start / Быстрый старт

Create a file `hello.osf`: / Создайте файл `hello.osf` :

`oyster print("Hello, Oyster!")`

Then run / Затем запустите:

```bash
./oyster hello.osf          # compile and run / компиляция и запуск
./oyster -c hello.osf       # compile only / только компиляция
./oyster -s hello.osf       # compile with source comments / с исходными комментариями
./oyster hello.oce          # run compiled bytecode / запуск скомпилированного
```

### Compiler Options / Опции компилятора
* -c Compile only, without execution / Только компиляция, без выполнения
* -s Add source lines as comments in bytecode / Добавлять исходные строки как комментарии
* -e Extended mode (float, edecimal) / Расширенный режим
* -o <file> Specify output file name / Указать имя выходного файла
* -I <path> Add path to @INC for module search / Добавить путь в @INC
* -h, --help Show help / Показать справку

## Example / Пример
```oyster
use "io" as io
use "math" as m 
@content = io.slurp("data.txt")
print("File size: " + len(@content))
$result = m.sin(&m.PI / 2)
print("sin(PI/2) = " + $result)
```

# Oyster Language v0.4.3

### Быстрый старт

```bash
# Сборка
gcc -o oyster main.c compiler.c vm.c -lm -O2

# Компиляция и запуск
./oyster script.osf

# Только компиляция
./oyster -c script.osf

# Компиляция с исходными комментариями в байт-коде
./oyster -s script.osf

# Расширенный режим (float, edecimal, postfix, методы)
./oyster -e script.osf

```

### Data Types / Типы данных

#### V_NUMBER (основной числовой тип)
* 64.32 fixed point. 64-bit integer part, 32-bit fractional part. Provides exact decimal fractions.
* 64.32 fixed point. Целая часть — 64 бита, дробная — 32 бита. Обеспечивает точные десятичные дроби.

```oyster
$x = 42          # nteger / целое
$y = 3.14        # with fractional part / с дробной частью
$z = 0.1 + 0.2   # 0.3 (exact! / точно!)
```

#### V_STRING
* Strings in double or single quotes, based on ByteArray.
* Строки в двойных или одинарных кавычках на основе ByteArray.

```oyster
$name = "Oyster"
$path = '/usr/local/bin'

$hw = "Hello,
word!" # Multi-line strings / Многострочные строки

$hw2 = "Hellow,\n word!" # Escape sequences / Escape-последовательности (\n, \t, \\, \", \', \r)

$unistring = u"My unicode string" + u" с кириллицей" # Unicode defined string / явное задание строки Unicode

```

#### V_ARRAY
* Fixed-length arrays based on ByteArray. Zero-based indexing.
* Массивы фиксированной длины на основе ByteArray. Индексация с 0.

```oyster
@arr = (10, 20, 30, 40)
$x = @arr[0]         # 10
@arr[1] = 99         # replace element / замена элемента
$len = len(@arr)     # 4

@arr2 = array(3)    # create empty array / создание пустого массива
```

#### V_HASH
* Hash on linked segments. Keys are strings, values are any type.
* Хеш на связных сегментах. Ключи — строки, значения — любые типы.

```oyster
%hash = (key1 => 100, key2 => "value")
$x = %hash["key1"]       # access element / доступ к элементу
%hash["key3"] = 300      # add/replace / добавление/замена
$exists = exists(%hash["key2"])  # create empty hash / проверка существования ключа

%hash = ()  # сщздание пустого хеша
hadd(%hash, "0.5.0", "version") 
print(%hash)
```
* Light hash — hash without keys. Created by assigning an array to a hash:
* Лёгкий хеш - хеш без ключей. Создаётся присваиванием массива хешу:
```oyster
%hash = ("one", "two", "three")
%hash = array(3)
```

#### V_UNDEF
* Undefined value. Used for uninitialized variables.
* Неопределённое значение. Используется для непроинициализированных переменных.

```oyster
$x = undef($y)           # check if undef / проверка на undef
$z = ifundef($maybe, 0)  # replace if undef / замена если undef
```

#### V_FLOAT (планируется для -e режима)
IEEE 754 double. Для совместимости с аппаратной арифметикой.

```oyster
$x = 3.14f
$y = $x.sqrt()
```


### Variables / Переменные
```oyster
$var — scalar / скалярная переменная

@arr — array / массив

%hash — hash / ххеш
```

* Variables are created on first assignment:
* Переменные создаются при первом присваивании:

```oyster
$x = 42
@data = (1, 2, 3)
%config = (debug => 1)
```

### Constants / Константы
* Named constants are substituted at compile time:
* Именованные константы подставляются на этапе компиляции:

```oyster
&PI = 3.14159
&MAX_SIZE = 100
&GREETING = "Hello"

$circumference = 2 * &PI * $radius
```

* External module constants via prefix:
* Константы из внешних модулей доступны через префикс:

```oyster
use "math" as M
$area = &M.PI * $radius * $radius
```

### Operators / Операторы
#### Arithmetic / Арифметические
* \+    # Сложение/конкатенация строк
* \-    # Вычитание
* \*    # Умножение
* /     # Деление
* %     # Остаток от деления
* ^     # (power/степень) Возведение в степень
* \-    # (unary/унарный) Отрицание
* \++   # Инкремент
* \--   # Декремент

#### Comparison / Сравнения
* ==    # Равно
* !=    # Не равно
* \<    # Меньше
* \>    # Больше
* \<=   # Меньше или равно
* \>=   # Больше или равно

#### Logical / Логические
* and   # Логическое И
* or    # Логическое ИЛИ
* not   # Логическое НЕ

#### Битовые
* &     # Битовое И
* \|    # Битовое ИЛИ
* ^^    # Битовое XOR
* \~    # Битовое НЕ
* \<<   # Сдвиг влево
* \>>   #Сдвиг вправо

#### Control Flow / Управляющие конструкции

if / elseif / else:

```oyster
if ($x > 10) {
    print($x)
} elseif ($x == 100) {
    print(100)
} elseif ($x == 300) {
    print(300)
} else {
    print(0)
}
```

#### Ternary / Тернарный оператор
```oyster
$max = ($a > $b) ? $a : $b
```

#### Loops / Циклы

while:
```oyster
$i = 0
while ($i < 10) {
    print($i)
    inc($i)
}
```

for (C-style):
```oyster
for ($i = 0; $i < 10; $i = $i + 1) {
    print($i)
}
```

for (in-style / по коллекции):
```oyster
@array = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
for $item in @array {
    print($item)
}
```

#### Loop Control / Управление циклом
* last      # exit loop / выход из цикла
* next      # next iteration / следующая итерация
* redo      # repeat current iteration / повтор текущей итерации

Labels for nested loops / Метки для вложенных циклов:
```oyster
OUTER: while ($i < 10) {
    INNER: for $j in @arr {
        if ($j == 5) { last OUTER }
        if ($j == 3) { next INNER }
    }
}
```

### Functions / Функции
#### User-defined / Пользовательские

Определение функции:

```oyster
fun add($a, $b) {
    return $a + $b
}

$result = add(10, 20)
```

* Space can be used as argument separator instead of comma:
* Пробел можно использовать вместо запятой:

```oyster
$result = add(10 20)


# Export from modules / Экспорт функций из модуля:

# In module / В модуле:
fun helper($x) {
    return $x * 2
}

export helper

# In main file / В основном файле:

use "mymodule" as M
print(M.helper(5))
```

Method calls / Методные вызовы:
```oyster
$x = $a.abs()          # abs($a)
$y = "hello".length()  # length("hello")
$z = 16.0.sqrt()       # sqrt(16.0)
$fh = "file.txt".fopen("r")
$arr = @data.revers()  # revers(@data)
$x.inc()               # инкремент
$x.dec()               # декремент
```

Method chains / Цепочки методов:
```oyster
$result = $a.abs().sqrt().int()
```


#### Built-in Functions / Встроенные функции

##### Печать
```oyster
print("Hello, World!")
```

##### Math / Математические
* abs(x)                # Абсолютное значение
* sign(x)               # Знак числа (-1, 0, 1)
* inv(x)                # Инверсия знака
* int(x)                # Целая часть числа
* frac(x)               # Дробная часть числа
* sqrt(x)               # Квадратный корень
* exp(x)                # Экспонента
* ln(x)                 # Натуральный логарифм
* log(x)                # Десятичный логарифм
* inc($x)               # Инкремент переменной
* dec($x)               # Декремент переменной

##### String / Строковые
* len(s)                    # Длина строки
* index(s, sub, pos)        # Поиск подстроки
* rindex(s, sub, pos)       # Поиск подстроки справа
* substr(s, off, len, repl) # Извлечение/замена подстроки
* chomp(s)                  # Убрать \n в конце
* chop(s)                   # Убрать последний символ
* lc(s)                     # Нижний регистр - только ASCII
* uc(s)                     # Верхний регистр - только ASCII
* lcfirst(s)                # Первый символ в нижний регистр - только ASCII
* ucfirst(s)                # Первый символ в верхний регистр - только ASCII
* split(pat, s)             # Разбить строку в массив
* join(sep, arr)            # Собрать массив в строку
* chr(n)                    # Код символа → символ
* ord(c)                    # Символ → код символа
* strcmp(s1, s2)            # Сравнение строк

##### Array/Hash/String / Для строк, массивов и хешей
* len(@arr)	            # Длина
* clone(@x)             # Клонировать
* deallocate(@x)	      # Освободить память

##### Array/String / Для строк и массивов
* get8/16/32/64(var, index)         # Чтение байт из ByteArray
* set8/16/32/64(var, index, value)  # Запись байт в ByteArray

##### Array / Для массивов
* revers(@arr)	        # Перевернуть массив
* sort(@arr)	          # Отсортировать массив

##### Hash / Для хешей
* exists(%hash["key"])          # Проверка существования ключа
* haskeys(%h)                   # Проверка наличия ключей. Отсутствие - признак лёгкого хеша
* setkey(%h, old_key, new_key)  # Изменить ключ
* getkey(%h, index)             # Получить ключ по индексу
* hadd(%h, value, key?)         # Добавить элемент в хеш
* hdel(%h)                      # Удалить последний элемент хеша
* hvalues(%h)                   # Массив значений хеша - ссылка
* hkeys(%h)                     # Массив ключей хеша - ссылка

##### Undef
* undef(x)              # Проверка: 1 если x — undef
* ifundef(x, default)   # x если не undef, иначе default

##### File / Файловые
* fopen(name, mode)         # Открыть файл
* fclose(fh)                # Закрыть файл
* freadline(fh)             # Прочитать строку
* fread(fh, len)            # Прочитать len байт
* fprint(fh, data)          # Записать в файл
* fseek(fh, offset, whence) # Позиционирование (whence: 0=начало, 1=текущая, 2=конец)
* ftell(fh)                 # Текущая позиция
* feof(fh)                  # Проверка конца файла
* sysread(fh, len)          # Системное чтение (аналогично fread)
* syswrite(fh, data)        # Системная запись (аналогично fprint)
* frename(old, new)         # Переименовать файл
* funlink(file)             # Удалить файл



### Postfix Notation / Постфиксная запись выражений
```oyster
$c = postfix{ $a $b + 2.0 / }
# Equivalent / Эквивалентно: $c = ($a + $b) / 2.0
```

### Modules / Модули

Подключение модуля:
```oyster
use "mymodule" as M
```

Вызов функции из модуля:
```oyster
print(M.myfunc(42))
```

Использование константы из модуля
```oyster
$x = &M.PI * 2
```

* Modules are compiled cascadingly: when compiling the main file, all dependencies are found and compiled automatically.
* Модули компилируются каскадно: при компиляции основного файла все зависимости находятся и компилируются автоматически.

### Примеры программ
#### Factorial / Факториал
```oyster
fun factorial($n) {
    if ($n <= 1) {
        return 1
    }
    return $n * factorial($n - 1)
}

print(factorial(10))
```

#### Sum of array elements / Сумма элементов массива
```oyster
@arr = (1, 2, 3, 4, 5)
$sum = 0
for $item in @arr {
    $sum = $sum + $item
}
print($sum)
```

#### Read file / Чтение файла
```oyster
$fh = fopen("data.txt" "r")
while (!feof($fh)) {
    $line = freadline($fh)
    print($line)
}
fclose($fh)
```

## Building from Source / Сборка из исходников

`make`  
`make test`  
`make install`  
`make clean`

## Dependencies / Зависимости

**Ubuntu/Debian:**  
`sudo apt install build-essential libpcre2-dev`

**Fedora/RHEL:**  
`sudo dnf install gcc make pcre2-devel`

## Author / Автор

**Daniil Kranchev**  
GitHub: [@denrav2019](https://github.com/denrav2019)  
Email: nnikus2017@gmail.com

## License / Лицензия

MIT License - see the LICENSE file for details.

## Acknowledgments / Благодарности

- Perl - or inspiration / за вдохновение
- Lua - for VM design concepts / за концепции дизайна VM
- The open source community / сообществу открытого ПО

## История релизов Oyster

### Oyster v0.4.3 — Завершение функционала файловых функций и строковых функций с поддержкой Unicode

Новое:

✅ index() — поддержка Unicode (UTF-32)

✅ rindex() — поддержка Unicode

✅ substr() — поддержка Unicode (O(1) доступ!)

✅ chop() — поддержка Unicode

✅ chomp() — поддержка Unicode

✅ Escape-последовательности в u"..."

✅ Файловые: feof, ftell, fseek, sysread, syswrite, frename, funlink


### Oyster v0.4.2 — Расширенный функционал в массиве и хеше

Новое:

✅ get8/16/32/64(var, index) — чтение байт из ByteArray

✅ set8/16/32/64(var, index, value) — запись байт в ByteArray

✅ hvalues(%h) — массив значений хеша

✅ hkeys(%h) — массив ключей хеша

✅ Добавлен , в стоп-лист для @ и %

Исправлено:

✅ get_var_index для имён с запятой

✅ Порядок аргументов в handle_get*

✅ val_number для get64
Новое:

✅ get8/16/32/64(var, index) — чтение байт из ByteArray

✅ set8/16/32/64(var, index, value) — запись байт в ByteArray

✅ hvalues(%h) — массив значений хеша

✅ hkeys(%h) — массив ключей хеша

✅ Добавлен , в стоп-лист для @ и %

Исправлено:

✅ get_var_index для имён с запятой

✅ Порядок аргументов в handle_get*

✅ val_number для get64


### Oyster v0.4.1 — поддержка Unicode

Новое:

✅ u"..." / u'...' — Unicode-строки (UTF-32 внутри)

✅ Маркер \uu\0 (4 байта) в начале Unicode-строк

✅ print() — автоматическое UTF-32 → UTF-8

✅ len() — подсчёт символов для Unicode

✅ + — конкатенация с приведением к Unicode

✅ clone() / deallocate() / chomp() / chop() — работают с Unicode

✅ Загрузка/сохранение .oce — строки с правильной длиной (без \0 обрезки)


### Oyster v0.4.0 — завершённый функционал в основном режиме

Новое:

✅ Перенос postfix и методов в обычный режим (уже работали)

✅ Методы для массивов (@arr.sort(), @arr.reverse())

✅ Пользовательские функции как методы ($x.add($y))

✅ exp(x), ln(x), log(x) для idouble

✅ clone($str) и deallocate($str) для строк

Исправлено:

✅ Исправлен стоп-лист для @ (добавлен .)

✅ Конфликт опкодов J (sqrt vs ifundef)

✅ ifundef → опкод Y

✅ handle_deallocate — убран дублирующийся V_HASH

✅ -k режим удалён из компилятора


### Changelog Oyster v0.3.6 — Управление циклами и локальные переменные
Новое:

✅ last/next/redo во всех циклах (while, for in, for C-style)

✅ Метки циклов (LOOP: while(...) { last LOOP })

✅ Индексация локальных переменных (fun можно определять в любом месте)

✅ Исправлены ошибки sqrt() и @arr[...] в print()

✅ Пробел как разделитель аргументов (везде)

Исправлено:

✅ Конфликт глобальных и локальных переменных в функциях

✅ body_start для redo в for in и for C-style

✅ Отложенный патчинг last_jumps и next_jumps для меток


### Changelog Oyster v0.3.5 — Циклы, хеши и escape-последовательности

✅ for $x in @arr — цикл по массиву
    
✅ %h["key"] в выражениях
    
✅ setkey(%h, old, new) — изменение ключа хеша
    
✅ Escape-последовательности (\n, \t, \\, \", \', \r)
    
✅ Многострочные строки
    
✅ elseif — каскадные условия
    
✅ Хеш-таблица пула строк (O(1), без дубликатов)

✅ Доступ к элементам хеша/массива по переменной-индексу (@arr[$idx])

✅ Конкатенация через +

Исправлено:

✅ hadd(%h, value) — добавление в лёгкий хеш

✅ %h = () — создание пустого хеша

✅ $ и @ с ] в стоп-листе

✅ sqrt() в print() — конфликт опкодов J


### Changelog Oyster v0.3.4 — elseif и хеш-таблица пула строк

Новое:

✅ elseif — каскадные условия (полноценный if/elseif/else)

✅ Хеш-таблица для пула строк (djb2 + открытая адресация, O(1))

✅ Устранены дубликаты строк в .oce

✅ ^ — возведение в степень

✅ + — конкатенация строк + сложение чисел (автоопределение типа)

✅ Режим -s (комментарии в байт-коде)

✅ Постфикс, методы, функции, рекурсия

Исправлено:

✅ parse_term — восстановлено тело цикла

✅ else if — не поддерживается, только elseif


### Changelog Oyster v0.3.3 — Математика и файлы

Новое:

✅ Регистронезависимый синтаксис

✅ Постфиксные выражения postfix{ ... }

✅ Методные вызовы $x.abs()

✅ Функции с параметрами и рекурсией

✅ return внутри if

✅ Базовые математические функции: abs, sign, int, frac, inv, sqrt (чистый idouble)

✅ inc $x / dec $x — инкремент/декремент переменной

✅ Конкатенация строк через + (автоопределение типа: строки → конкатенация, числа → сложение)

✅ ^ — возведение в степень (заменил **)

✅ Файловые функции: fopen, fclose, freadline, fread, fprint (вторая dispatch-таблица)

✅ Унарный минус

Исправлено:

✅ Переполнение в handle_print для дробных чисел (128-битное умножение)

✅ Конфликт опкодов I (int vs inc)

✅ handle_deallocate — дублирующийся V_HASH


### Changelog Oyster v0.3.2 — Пул строк
Новое:

✅ Поиск дубликатов в str_pool_buf (устранение повторов строк в .oce)

✅ Индекс строки = позиция в выходном буфере (не в string_pool)

Исправлено:

✅ Восстановление str_pool_pos после компиляции функций

✅ parse_term — восстановлено тело цикла (было пустым)


### Changelog Oyster v0.3.1 — Функции и строки
Новое:

✅ fun с параметрами и возвратом значений

✅ Рекурсивные функции (func_table до компиляции тела)

✅ return внутри if и общего parse_statement

✅ Строковые функции (14 отдельных опкодов): len, lc, uc, chr, ord, chomp, chop, lcfirst, ucfirst, index, rindex, substr, split, join

✅ Конкатенация строк (.)

✅ Сравнение строк (strcmp)

✅ Оператор ^ для возведения в степень

✅ Регистронезависимый синтаксис (FUN, Fun, fun)

✅ Постфиксные выражения postfix{ ... }

✅ Методные вызовы $x.abs(), $s.uc()

✅ -s режим (комментарии в байт-коде)

Исправлено:

✅ Однопроходная компиляция функций (тело в отдельный буфер)

✅ Порядок байт-кода (<E:...> перед функциями)

✅ Stack underflow после возврата из основного модуля


### Changelog v0.2.0 — Модульная система и компилятор:
Новое:

✅ Прямой формат ТДФ (<f:add:math:02:02:00000000:1>) — без индексов в пуле строк

✅ Прямой формат таблицы констант (<d:PI:0000000000000003:243F3E03>)

✅ Система модулей: use "math" as M, export

✅ Каскадная компиляция модулей

✅ Константы из внешних модулей (&Math.PI)

✅ Вызов внешних функций (Math.add(x, y))

✅ Ленивая загрузка модулей при первом вызове

✅ undef — литерал неопределённого значения

✅ undef(x) — проверка на undef

✅ ifundef(x, default) — замена если undef

✅ Базовые структуры для массивов и хешей

Исправлено:

✅ Порядок вызова get_var_index (сначала чтение имени, потом вызов)

✅ Запятая в имени переменной ($x, → $x)

✅ Пересборка ТДФ с учётом export_flag

✅ Формат умножения/деления для fixed-point

✅ Загрузка модулей в VM

✅ Парсинг .ocm файлов

✅ exists(%h, key) для хешей

