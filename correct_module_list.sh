#!/usr/bin/sh

get_symbol()
{
    echo "$1" | grep vlc_entry_$2|cut -d" " -f 3
}

checkfail()
{
    if [ ! $? -eq 0 ];then
        echo "$1"
        exit 1
    fi
}

create_replace_symbol_list()
{
    #Regular expression to check that we're matching something
    #preceded by a '@' or '.'
    local NPBCI='([@.])' PREV='\\1'
    local name=$1 entry=$2 copyright=$3 license=$4
    for line in $(cat <<EOF
AccessOpen
AccessClose
StreamOpen
StreamClose
DemuxOpen
DemuxClose
OpenFilter
CloseFilter
Open
Close
EOF
                 );
    do
        echo "s/$NPBCI$line/$PREV${line}__$name/g" >> symbol_list;
    done;
    if [ ! "$entry" = "vlc_entry_$name" ]; then
        echo "s/$NPBCI$entry/${PREV}vlc_entry__$name/g" >> symbol_list;
    fi;
    if [ ! -z $copyright ]; then
        echo "s/$NPBCI$copyright/${PREV}vlc_entry_copyright__$name/g" >> symbol_list;
    fi;
    if [ ! -z $license ]; then
        echo "s/$NPBCI$license/${PREV}vlc_entry_license__$name/g" >> symbol_list;
    fi;
}

alter_library_symbols()
{
    local file name entry copyright license

    file=$1
    name=`echo $file | sed 's/.*\.libs\/lib//' | sed 's/_plugin\.a//'`
    echo "Renaming symbols in : $name"
    symbols="$($NM -g $file)"
    entry=$(get_symbol "$symbols" _)
    copyright=$(get_symbol "$symbols" copyright)
    license=$(get_symbol "$symbols" license)
    mkdir module_tmp_folder_$name;
    checkfail "[$name]: couldn't create temporary folder"
    cd module_tmp_folder_$name;

    create_replace_symbol_list $name $entry $copyright $license
    ar x $file;
    checkfail "[$name]: couldn't extract archive"
    for obj_file in $(ls *.o | tr ' ' '\n');
    do
        echo " [$name]: update " $obj_file;
        $OPT -S $obj_file | sed -rf symbol_list | $OPT - -o $obj_file.tmp;
        checkfail "[$name]: $obj_file failed on symbol renaming"
        mv $obj_file.tmp $obj_file;
    done;
    ar crs $file *.o;
    checkfail "[$name]: couldn't create archive"
    $RANLIB $file;
    checkfail "[$name]: ranlib failed"
    cd ..
    rm -fr module_tmp_folder_$name;
    exit 0;
}

cd $MODULES_RENAMING;
alter_library_symbols $1
