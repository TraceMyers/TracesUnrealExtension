
main :: () {
    collector: Path_Collector;
    init(*collector.seen_asset_name, 2048);

    exe_path := parse_path(get_path_of_running_executable());
    project_path := path_to_string(exe_path, exe_path.words.count-4);

    visit_path := tprint("%Content", project_path);
    visit_files(visit_path, true, *collector, visitor);

    if collector.paths.count == 0 {
        return;
    }

    builder: String_Builder;
    print(*builder, "#pragma once\n\n");
    for 0..collector.paths.count-1 {
        path := collector.paths[it];
        name := collector.names[it];
        print(*builder, "inline const wchar_t* ASSET_% = L\"%\";\n", name, path);
    }

    directory_path := tprint("%Plugins/TracesUnrealExtension/Source/TracesUnrealExtension/Public/Generated/", project_path);
    make_directory_if_it_does_not_exist(directory_path, true);

    file_path := tprint("%ProjectAssetReferences.h", directory_path);

    write_entire_file(file_path, *builder);
}

Path_Collector :: struct {
    seen_asset_name: Table(string, void);
    paths: [..]string;
    names: [..]string;
}

fix_name :: (str: *string) {
    for *c : str.* {
        if !is_alnum(c.*) {
            c.* = #char "_";
        }
    }
}

visitor :: (info: *File_Visit_Info, collector: *Path_Collector) {
    if ends_with(info.short_name, ".uasset") {
        asset_name := split(info.short_name, #char ".")[0];

        fixed_asset_name := to_upper_copy(asset_name);
        fix_name(*fixed_asset_name);

        if table_contains(*collector.seen_asset_name, fixed_asset_name) {
            log("Trace's Unreal Extension/generate_asset_paths.exe: duplicate asset names will be ignored during auto-asset reference management. ignoring duplicate asset %", asset_name);
            return;
        }

        table_add(*collector.seen_asset_name, fixed_asset_name, #run{});

        content_relative_path := split(info.full_name, "Content/");
        assert(content_relative_path.count == 2);
        success, game_relative_path, right := split_from_right(content_relative_path[1], #char "/");
        assert(success);

        path_builder: String_Builder;
        print(*path_builder, "/Game");
        print(*path_builder, "/%", game_relative_path);
        print(*path_builder, "/%.%", asset_name, asset_name);

        asset_path := builder_to_string(*path_builder);

        array_add(*collector.paths, asset_path);
        array_add(*collector.names, fixed_asset_name);
    }
}


#import "Basic";
#import "File";
#import "File_Utilities";
#import "String";
#import "Hash_Table";
#import "System";
