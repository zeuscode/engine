// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6

import 'dart:io';

import 'package:args/args.dart';
import 'package:path/path.dart' as path;

final ArgParser argParser = ArgParser()
  ..addOption('output-dir')
  ..addOption('input-dir')
  ..addFlag('ui', defaultsTo: false)
  ..addFlag('engine', defaultsTo: false)
  ..addMultiOption('input')
  ..addOption('stamp');

final List<Replacer> uiPatterns = <Replacer>[
  AllReplacer(RegExp(r'library\s+ui;'), 'library dart.ui;'),
  AllReplacer(RegExp(r'part\s+of\s+ui;'), 'part of dart.ui;'),
  AllReplacer(RegExp(r'''
import\s*'src/engine.dart'\s*as\s+engine;
'''), r'''
import 'dart:_engine' as engine;
'''),
  AllReplacer(RegExp(
    r'''
export\s*'src/engine.dart'
'''),
    r'''
export 'dart:_engine'
''',
  ),
];

final List<Replacer> engineLibraryPatterns = <Replacer>[
  AllReplacer(RegExp(r'library\s+engine;'), 'library dart._engine;'),
  AllReplacer(RegExp(r'''
import\s*'../ui.dart' as ui;
'''), r'''
import 'dart:ui' as ui;
'''),
  AllReplacer(RegExp(r'''
import\s*'package:ui/ui.dart' as ui;
'''), r'''
import 'dart:ui' as ui;
'''),
  // Remove imports of engine part files.
  AllReplacer(RegExp(r"import\s*'engine/.*';"), ''),
  // Replace exports of engine files with "part" directives.
  MappedReplacer(RegExp(r'''
export\s*'engine/(.*)';
'''), (Match match) => '''
part 'engine/${match.group(1)}';
'''),
  AllReplacer(
    RegExp(r"import\s*'package:js/js.dart'"),
    r"import 'dart:_js_annotations'",
  ),
];

final List<Replacer> enginePartsPatterns = <Replacer>[
  AllReplacer(RegExp(r'part\s+of\s+engine;'), 'part of dart._engine;'),
  // Remove library-level JS annotations.
  AllReplacer(RegExp(r'\n@JS(.*)\nlibrary .+;'), ''),
  // Remove library directives.
  AllReplacer(RegExp(r'\nlibrary .+;'), ''),
  // Remove imports/exports from all engine parts.
  AllReplacer(RegExp(r'\nimport\s*.*'), ''),
  AllReplacer(RegExp(r'\nexport\s*.*'), ''),
];

final List<Replacer> sharedPatterns = <Replacer>[
  AllReplacer(RegExp(r"import\s*'package:meta/meta.dart';"), ''),
  AllReplacer('@required', ''),
  AllReplacer('@protected', ''),
  AllReplacer('@mustCallSuper', ''),
  AllReplacer('@immutable', ''),
  AllReplacer('@visibleForTesting', ''),
];

// Rewrites the "package"-style web ui library into a dart:ui implementation.
// So far this only requires a replace of the library declarations.
void main(List<String> arguments) {
  final ArgResults results = argParser.parse(arguments);
  final Directory directory = Directory(results['output-dir'] as String);
  final String inputDirectoryPath = results['input-dir'] as String;
  for (final String inputFilePath in results['input'] as Iterable<String>) {
    final File inputFile = File(inputFilePath);
    final File outputFile = File(path.join(
        directory.path, inputFile.path.substring(inputDirectoryPath.length)))
      ..createSync(recursive: true);
    final String source = inputFile.readAsStringSync();
    final String rewrittenContent = rewriteFile(
      source,
      filePath: inputFilePath,
      isUi: results['ui'] as bool,
      isEngine: results['engine'] as bool,
    );
    outputFile.writeAsStringSync(rewrittenContent);
    if (results['stamp'] != null) {
      File(results['stamp'] as String).writeAsStringSync('stamp');
    }
  }
}

String rewriteFile(String source, {String filePath, bool isUi, bool isEngine}) {
  final List<Replacer> replacementPatterns = <Replacer>[];
  replacementPatterns.addAll(sharedPatterns);
  if (isUi) {
    replacementPatterns.addAll(uiPatterns);
  } else if (isEngine) {
    if (filePath.endsWith('lib/src/engine.dart')) {
      replacementPatterns.addAll(engineLibraryPatterns);
    } else {
      source = _preprocessEnginePartFile(source);
      replacementPatterns.addAll(enginePartsPatterns);
    }
  }
  for (final Replacer replacer in replacementPatterns) {
    source = replacer.perform(source);
  }
  return source;
}

String _preprocessEnginePartFile(String source) {
  if (source.startsWith('part of engine;') || source.contains('\npart of engine;')) {
    // The file hasn't been migrated yet.
    // Do nothing.
  } else {
    // Insert the part directive at the beginning of the file.
    source = 'part of engine;\n' + source;
  }
  return source;
}

/// Responsible for performing string replacements.
abstract class Replacer {
  /// Performs the replacement in the provided [text].
  String perform(String text);
}

/// Replaces all occurrences of a pattern with a fixed string.
class AllReplacer implements Replacer {
  /// Creates a new tuple with the given [pattern] and [replacement] string.
  AllReplacer(this._pattern, this._replacement);

  /// The pattern to be replaced.
  final Pattern _pattern;

  /// The replacement string.
  final String _replacement;

  @override
  String perform(String text) {
    return text.replaceAll(_pattern, _replacement);
  }
}

/// Uses a callback to replace each occurrence of a pattern.
class MappedReplacer implements Replacer {
  MappedReplacer(this._pattern, this._replace);

  /// The pattern to be replaced.
  final RegExp _pattern;

  /// A callback to replace each occurrence of [_pattern].
  final String Function(Match match) _replace;

  @override
  String perform(String text) {
    return text.replaceAllMapped(_pattern, _replace);
  }
}
