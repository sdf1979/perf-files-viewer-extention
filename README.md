# perf-files-viewer-extention
Внешняя обработка с внешней NativeAPI компонентой просмотра двоичных файлов "Perfomance monitor". Платформа 1С x32, x64 не ниже 8.3.18, только ОС Windows.
Внешняя обработка с внешней NativeAPI компонентой просмотра двоичных файлов "Perfomance monitor". Позволяет строить диаграмму по данным из двоичных файлов (Можно открыть как единое целое одновременно до 32 файлов). Отбор СКД по именам счетчиков производительности. Изменение видимости счетчиков на диаграмме, изменение цвета серии данных, изменение толщины серии и масштаба.

В отличии от стандартной программы "Perfomance monitor", встроенной в ОС семейства Windows , данная обработка выводит в точку графика максимальное значение за временной период, которому соответствует данная точка (стандартная программа выводит на график в точку среднее за период). Вывод максимальных значений позволяет акцентировать внимание на моменты пиковых нагрузок.

Особенности: на текущий момент отсутствующие значения интерпретируются как ноль и отображаются на диаграмме.

При написании внешней обработки использовался шаблон Modern Native AddIn от https://github.com/Infactum/addin-template.

Обработка протестирована в тонком клиенте x32, x64 на платформе 8.3.18.1563, 8.3.20.1838.
