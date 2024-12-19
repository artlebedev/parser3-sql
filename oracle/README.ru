    ^connect[protocol://строка соединения]]{код с ^sql[...]-ями}

        oracle://user:pass@service?
            NLS_LANG=RUSSIAN_AMERICA.CL8MSWIN1251&
            NLS_LANGUAGE  language-dependent conventions
            NLS_TERRITORY  territory-dependent conventions
            NLS_DATE_FORMAT=YYYY-MM-DD HH24:MI:SS
            NLS_DATE_LANGUAGE  language for day and month names
            NLS_NUMERIC_CHARACTERS  decimal character and group separator
            NLS_CURRENCY  local currency symbol
            NLS_ISO_CURRENCY  ISO currency symbol
            NLS_SORT  sort sequence
            ORA_ENCRYPT_LOGIN=TRUE
            ClientCharset=parser-charset << charset in which parser thinks client works

#sql drivers
$SQL[
    $.drivers[^table::create{protocol	driver	client
oracle	/www/parser3/libparser3oracle.so	/u01/app/oracle/product/8.1.5/lib/libclntsh.so?ORACLE_HOME=/u01/app/oracle/product/8.1.5&ORA_NLS33=/u01/app/oracle/product/8.1.5/ocommon/nls/admin/data
}]
]

        в столбце клиентской библиотеки
        допустимо задать environment параметры инициализации(если они не заданы иначе заранее),
        допустимы имена, начинающиеся на NLS_ ORA_ и ORACLE_, или оканчивающиеся на +
        под win32 
            необходим PATH+=^;C:\Oracle\Ora81\bin
        к сведению:
          ORA_NLS33 нужен для считывания файлика с клиентской кодировкой(задаваемой NLS_LANG)
             если кодировка не по-умолчанию, обязательно указать в .drivers,
             иначе будет сообщение про неправильный NLS параметр
             (имеют в виду, что не нашли кодировку из NLS_LANG)
          ORACLE_HOME нужен для считывания текстов сообщений об ошибках,
        можно указывать и в строке соединения, но глобален, и лучше вынести за скобки,
        в отличие от клиентской кодировки NLS_LANG, и прочего.

        ВНИМАНИЕ: при работе с большими текстовыми блоками в oracle,
        ставить такой префикс перед открывающим апострофом, впритык, везде без проблелов
        /**имя_поля**/'literal'


$Id: README.ru,v 1.1 2024/12/19 23:20:08 moko Exp $
