"""Run with UnrealEditor-Cmd -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
    -run=pythonscript -script=<absolute path to this file>.
"""
import json
from pathlib import Path
import unreal
root=Path(__file__).resolve().parents[2]/'Assets/Audio/GeometricWarfare'
manifest=json.loads((root/'manifest.json').read_text())
for item in manifest:
    task=unreal.AssetImportTask()
    task.filename=str(root/(item['name']+'.wav'))
    task.destination_path='/Game/Audio/GeometricWarfare'
    task.automated=True;task.replace_existing=True;task.save=True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset=unreal.load_asset(task.destination_path+'/'+item['name'])
    if not isinstance(asset,unreal.SoundWave):raise RuntimeError('Missing SoundWave '+item['name'])
    asset.set_editor_property('looping',item['loop'])
    if item['loop']:asset.set_editor_property('virtualization_mode',unreal.VirtualizationMode.PLAY_WHEN_SILENT)
    asset.set_editor_property('loading_behavior',unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    asset.set_editor_property('compression_quality',70)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log(f'GW_AUDIO_IMPORTED={len(manifest)}')
