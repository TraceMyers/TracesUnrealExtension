// This source was probably used to generate the executable binary in the same directory. The exe creates a header file
// available to your Unreal project. The file has a const TCHAR* path for every asset in the main module. This allows a programmer-
// oriented workflow where you can refer directly to assets in code without a laborious Unreal abstraction layer in-between.
// If the designers can do minor code edits, I wouldn't consider it designer-hostile either. Removing an asset that is referenced
// in code creates a clear compile error associated with specific lines of code, so an opportunity is made for anybody on the team 
// to fix dangling asset references.

// Every invocation of this process requires the format ./this_binary.exe -content_root path/to/content -mount_root relative/game/root -output path/to/output/header.h
// The arguments can be in any order. 

// -content_root should include "/Content" at the end.
// -mount_root is usually just "/Game".
// -output should end with the generated header file's name. the path does not need to exist before invocation.

// The .uplugin file includes an exe invocation with reasonable default paths as a plugin pre-build step. So, you shouldn't 
// need to do anything for it to work.

// As of time of writing (8/21/26), you either need a windows machine or access to the jai compiler to use this program,
// because the packaged binary is windows-only, but the source is provided. If the program is broken for you, please make an Issue 
// at https://github.com/TraceMyers/TracesUnrealExtension or email me at main@tracemyers.com. Or obviously feel free to edit and 
// recompile if you have the compiler. Code is owned by everybody.

// If you can't or don't want to use this feature, remove the PreBuildSteps section from the .uplugin file.

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

    parse_success, result, is_set, free_arguments := parse_arguments(Command_Line_Arguments);
    validate(parse_success, .FAILED_PARSE_COMMAND_LINE_ARGUMENTS, "failed to parse command line args %", cl_args_array);

    all_args_passed := is_set.content_root && is_set.mount_root && is_set.output;
    validate(all_args_passed, .INVALID_ARGUMENT_NAME, "at least one argument was not named correctly. %", FormatStruct.{value=is_set, use_long_form_if_more_than_this_many_members=0});

    cl_args = result;

    convert_slashes(cl_args.content_root);
    convert_slashes(cl_args.mount_root);
    convert_slashes(cl_args.output);

    content_root_exists := file_exists(cl_args.content_root);
    validate(content_root_exists, .PATH_DOES_NOT_EXIST, "content root path % does not exist", cl_args.content_root);

    // /Game
    cl_args.mount_root = tprint("/%", trim(cl_args.mount_root, "/")); 
    // .../Content/
    cl_args.content_root = tprint("%/", trim_right(cl_args.content_root, "/"));

    collector: Path_Collector;

    // collect asset paths and names
    visit_success := visit_files(cl_args.content_root, true, *collector, visitor);
    validate(visit_success, .FAILED_TO_SEARCH_CONTENT_DIRECTORY, "failed to search the content directory %", cl_args.content_root);

    output_path := parse_path(cl_args.output);

    output_path_last_word         := output_path.words[output_path.words.count-1];
    last_word_is_header_extension := ends_with(output_path_last_word, ".h") || ends_with(output_path_last_word, ".hpp");
    validate(last_word_is_header_extension, .OUTPUT_PATH_NOT_HEADER, "output path % should end with a c/cpp header file, but does not end with .h or .hpp", cl_args.output);

    output_folder_path   := path_to_string(output_path, output_path.words.count-1);
    output_folder_exists := make_directory_if_it_does_not_exist(output_folder_path, true);
    validate(output_folder_exists, .FAILED_TO_MAKE_GENERATED_DIRECTORY, "failed to make generated directory at %", output_folder_path);

    // sort assets for deterministic (except for duplicates) order.
    // file traversal order is not guaranteed to be the same every time on windows.
    // sorted by path to make duplicate-named assets deterministically sorted.
    quick_sort(collector.assets, (a, b) => compare(a.path, b.path));

    seen_asset_name: Table(string, void);
    init(*seen_asset_name, 2048);

    // generate the header file
    builder: String_Builder;
    print(*builder, "#pragma once\n\n");

    for 0..collector.assets.count-1 {
        path := collector.assets[it].path;
        name := collector.assets[it].name;

        // duplicate asset names would create duplicate identifiers. give the user a warning that duplicates are ignored.
        // note that names are normalized to be all-upper
        if table_contains(*seen_asset_name, name) {
            log_warning("duplicate asset names will be ignored during auto-asset reference management. ignoring duplicate asset %", path);
            continue;
        }

        // #run{} returns void. the table is being used as a set, so there is no value in the key, value pair.
        table_add(*seen_asset_name, name, #run{}); 

        print(*builder, "inline const TCHAR* ASSET_% = TEXT(\"%\");\n", name, path);
    }

    new_file_text := builder_to_string(*builder,, temp);
    old_file_text, read_success := "", false;

    if file_exists(cl_args.output) {
        old_file_text, read_success = read_entire_file(cl_args.output,, temp);
    }

    // check for changes before writing
    if read_success && new_file_text == old_file_text {
        log_normal("no asset changes detected.");
    } else {
        log_normal("writing asset refs to %", cl_args.output);
        write_success := write_entire_file(cl_args.output, new_file_text);
        validate(write_success, .FAILED_TO_WRITE_ASSETS_FILE, "write failed");
    }
}

Asset :: struct {
    name: string;
    path: string;
}

Path_Collector :: struct {
    assets: [..]Asset;
}

fix_name :: (str: string) {
    if str.count > 0 {
        if !is_alpha(str[0]) {
            str[0] = #char "_";
        }
    } else return;

    if str.count > 1 then for 1..str.count-1 {
        if !is_alnum(str[it]) {
            str[it] = #char "_";
        }
    }
}

visitor :: (info: *File_Visit_Info, collector: *Path_Collector) {
    if ends_with(info.short_name, ".uasset") || ends_with(info.short_name, ".umap") {
        asset_name := split(info.short_name, #char ".")[0];

        fixed_asset_name := to_upper_copy(asset_name);
        fix_name(fixed_asset_name);

        content_relative_path := split(info.full_name, cl_args.content_root);

        assert(content_relative_path.count == 2);
        game_relative_path_is_not_top_level, game_relative_path, right := split_from_right(content_relative_path[1], #char "/");
        game_relative_path = trim(game_relative_path, "/");

        path_builder: String_Builder;
        print(*path_builder, cl_args.mount_root);

        if game_relative_path_is_not_top_level {
            print(*path_builder, "/%", game_relative_path);
        }

        print(*path_builder, "/%.%", asset_name, asset_name);

        asset_path := builder_to_string(*path_builder);

        array_add(*collector.assets, {name=fixed_asset_name, path=asset_path});
    }
}

LOG_HEADER :: "Traces Unreal Extension / generate asset paths\n\t";

log_normal ::  (fmt: string, args: ..Any) {
    error_fmt := tprint("[LOG]     %%", LOG_HEADER, fmt);
    log(error_fmt, ..args);
}

log_warning :: (fmt: string, args: ..Any) {
    error_fmt := tprint("[WARNING] %%", LOG_HEADER, fmt);
    log(error_fmt, ..args);
}

log_error :: (fmt: string, args: ..Any) {
    error_fmt := tprint("[ERROR]   %%", LOG_HEADER, fmt);
    log(error_fmt, ..args);
}

convert_slashes :: (path: string) {
    for *c : path {
        if c.* == #char "\\" {
            c.* = #char "/";
        }
    }

}

validate :: (condition: bool, error_code: Error_Code, message_fmt: string, message_args: ..Any) {
    if !condition {
        log_error(message_fmt, ..message_args);
        exit(xx error_code);
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
