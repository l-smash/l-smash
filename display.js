function toggle_display( obj_name )
{
    var obj = (document.all && document.all( obj_name ))
           || (document.getElementById && document.getElementById( obj_name ));
    if( obj && obj.style )
    {
        if( obj.style.display == "none" )
            obj.style.display = "";
        else
            obj.style.display = "none";
    }
}

function toggle_display_org( obj_name )
{
    var obj_name_0 = obj_name + "_0";
    var obj_name_1 = obj_name + "_1";
    var obj_name_2 = obj_name + "_2";
    toggle_display( obj_name_0 );
    toggle_display( obj_name_1 );
    toggle_display( obj_name_2 );
}

function last_modified()
{
    var date = new Date( document.lastModified );
    var y = date.getFullYear();
    var m = date.getMonth() + 1;
    var d = date.getDate();
    return "Last modified : " + y + "/" + m + "/" + d;
}
