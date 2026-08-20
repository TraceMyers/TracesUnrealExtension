Command_Line_Arguments :: struct {
    content_root: string;
    mount_root: string;
    output: string;
}

Error_Code :: enum s32 {
    FAILED_PARSE_COMMAND_LINE_ARGUMENTS :: 1;
    INVALID_ARGUMENT_NAME;
    OUTPUT_PATH_NOT_HEADER;
    PATH_DOES_NOT_EXIST;
    FAILED_TO_WRITE_ASSETS_FILE;
    FAILED_TO_SEARCH_CONTENT_DIRECTORY;
    FAILED_TO_MAKE_GENERATED_DIRECTORY;
};

cl_args: Command_Line_Arguments;

main :: () {
    cl_args_array := get_command_line_arguments();

    success, result, is_set, free_arguments := parse_arguments(Command_Line_Arguments);

    if !success {
        log_error("failed to parse command line args %", cl_args_array);
        exit(xx Error_Code.FAILED_PARSE_COMMAND_LINE_ARGUMENTS);
    }

    if !is_set.content_root || !is_set.mount_root || !is_set.output {
        log_error("at least one argument was not named correctly.");
        log_error("%", FormatStruct.{value=is_set, use_long_form_if_more_than_this_many_members=0});
        exit(xx Error_Code.INVALID_ARGUMENT_NAME);
    }

    cl_args = result;

    convert_slashes(cl_args.content_root);
    convert_slashes(cl_args.mount_root);
    convert_slashes(cl_args.output);

    exit_if_invalid_path("content_root");

    // /Game
    cl_args.mount_root = tprint("/%", trim(cl_args.mount_root, "/")); 
    // .../Content/
    cl_args.content_root = tprint("%/", trim_right(cl_args.content_root, "/"));

    collector: Path_Collector;
    init(*collector.seen_asset_name, 2048);

    if !visit_files(cl_args.content_root, true, *collector, visitor) {
        log_error("failed to search the content directory %", cl_args.content_root);
        exit(xx Error_Code.FAILED_TO_SEARCH_CONTENT_DIRECTORY);
    }

    output_path := parse_path(cl_args.output);

    output_path_last_word := output_path.words[output_path.words.count-1];
    if !ends_with(output_path_last_word, ".h") && !ends_with(output_path_last_word, ".hpp") {
        log_error("output path % should end with a c/cpp header file, but does not end with .h or .hpp", cl_args.output);
        exit(xx Error_Code.OUTPUT_PATH_NOT_HEADER);
    }

    output_folder_path := path_to_string(output_path, output_path.words.count-1);
    if !make_directory_if_it_does_not_exist(output_folder_path, true) {
        log_error("failed to make generated directory at %", output_folder_path);
        exit(xx Error_Code.FAILED_TO_MAKE_GENERATED_DIRECTORY);
    }

    builder: String_Builder;
    print(*builder, "#pragma once\n\n");

    // sort assets for deterministic (except for duplicates) order.
    // windows traversal order is not guaranteed to be the same every time.
    quick_sort(collector.assets, (a, b) => compare(a.name, b.name));

    for 0..collector.assets.count-1 {
        path := collector.assets[it].path;
        name := collector.assets[it].name;
        print(*builder, "inline const TCHAR* ASSET_% = TEXT(\"%\");\n", name, path);
    }

    new_file_text := builder_to_string(*builder,, temp);
    old_file_text, read_success := "", false;

    if file_exists(cl_args.output) {
        old_file_text, read_success = read_entire_file(cl_args.output,, temp);
    }

    if read_success && new_file_text == old_file_text {
        log_normal("no asset changes detected.");
    } else {
        log_normal("writing asset refs to %", cl_args.output);
        if !write_entire_file(cl_args.output, new_file_text) {
            log_error("write failed.");
            exit(xx Error_Code.FAILED_TO_WRITE_ASSETS_FILE);
        }
    }
}

Asset :: struct {
    name: string;
    path: string;
}

Path_Collector :: struct {
    seen_asset_name: Table(string, void);
    assets: [..]Asset;
}

fix_name :: (str: *string) {
    for *c : str.* {
        if !is_alnum(c.*) {
            c.* = #char "_";
        }
    }
}

visitor :: (info: *File_Visit_Info, collector: *Path_Collector) {
    if ends_with(info.short_name, ".uasset") || ends_with(info.short_name, ".umap") {
        asset_name := split(info.short_name, #char ".")[0];

        fixed_asset_name := to_upper_copy(asset_name);
        fix_name(*fixed_asset_name);

        if table_contains(*collector.seen_asset_name, fixed_asset_name) {
            log_warning("duplicate asset names will be ignored during auto-asset reference management. ignoring duplicate asset %", asset_name);
            return;
        }

        table_add(*collector.seen_asset_name, fixed_asset_name, #run{});

        content_relative_path := split(info.full_name, cl_args.content_root);

        assert(content_relative_path.count == 2);
        game_relative_path_is_not_top_level, game_relative_path, right := split_from_right(content_relative_path[1], #char "/");
        game_relative_path = trim(game_relative_path, "/");

        path_builder: String_Builder;
        print(*path_builder, cl_args.mount_root);

        if !game_relative_path_is_not_top_level {
            print(*path_builder, "/%", game_relative_path);
        }

        print(*path_builder, "/%.%", asset_name, asset_name);

        asset_path := builder_to_string(*path_builder);

        array_add(*collector.assets, {name=fixed_asset_name, path=asset_path});
    }
}

exit_if_invalid_path :: ($field_name: string) #expand {
    VAR_PATH :: #run tprint("cl_args.%", field_name);
    path := #insert #run tprint("%;", VAR_PATH);
    exists := file_exists(path);
    if !exists {
        #insert #run tprint("log_error(\"% path \% does not exist\", %);", VAR_PATH, VAR_PATH);
        exit(xx Error_Code.PATH_DOES_NOT_EXIST);
    }
}

log_normal ::  (fmt: string, args: ..Any) {
    NORMAL_HEADER :: "[LOG] TracesUnrealExtension/generate_asset_paths: ";
    error_fmt := tprint("%%", NORMAL_HEADER, fmt);
    log(error_fmt, ..args);
}

log_warning :: (fmt: string, args: ..Any) {
    WARNING_HEADER :: "[WARNING] TracesUnrealExtension/generate_asset_paths: ";
    error_fmt := tprint("%%", WARNING_HEADER, fmt);
    log(error_fmt, ..args);
}

log_error :: (fmt: string, args: ..Any) {
    ERROR_HEADER :: "[ERROR] TracesUnrealExtension/generate_asset_paths: ";
    error_fmt := tprint("%%", ERROR_HEADER, fmt);
    log(error_fmt, ..args);
}

convert_slashes :: (path: string) {
    for *c : path {
        if c.* == #char "\\" {
            c.* = #char "/";
        }
    }

}

#import "Basic";
#import "File";
#import "File_Utilities";
#import "String";
#import "Hash_Table";
#import "System";
#import "Command_Line";
#import "Sort";
