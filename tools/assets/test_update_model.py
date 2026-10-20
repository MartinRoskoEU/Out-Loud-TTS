"""Focused offline policy and transaction fault tests. No installed state or network."""
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import update_model as updater


class VersionTests(unittest.TestCase):
    def test_minor_from_authority(self):
        self.assertEqual(updater.next_version('4.19.7'), '4.20.0')
        self.assertEqual(updater.next_version('0.0.0'), '0.1.0')

    def test_invalid_or_exhausted_version(self):
        for value in ('01.2.3', '1.2.3-beta', '1.65534.2', '65535.0.0'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                updater.next_version(value)


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.work = self.root / 'generated/model-updates'; self.work.mkdir(parents=True)
        self.transaction = self.work / 'transaction'; self.transaction.mkdir()
        self.stage = self.transaction / 'source'; self.stage.mkdir()
        self.patches = [patch.object(updater, 'ROOT', self.root), patch.object(updater, 'WORK', self.work),
                        patch.object(updater, 'PENDING', self.work / 'pending.json')]
        for item in self.patches: item.start()
        for name in ('pin.json', 'ProductVersion.props'):
            (self.root / name).write_text('previous-'+name)
            (self.stage / name).write_text('candidate-'+name)
        self.before = {name: updater.sha(self.root / name) for name in ('pin.json', 'ProductVersion.props')}

    def tearDown(self):
        for item in reversed(self.patches): item.stop()
        self.temporary.cleanup()

    def publish(self):
        updater.publish(self.stage, self.transaction, ['pin.json', 'ProductVersion.props'], self.before)

    def assert_previous(self):
        for name, digest in self.before.items(): self.assertEqual(updater.sha(self.root / name), digest)
        self.assertFalse(updater.PENDING.exists())

    def test_validation_failure_rolls_back_all_files(self):
        with patch.object(updater, 'local_identity', side_effect=ValueError('native failed')):
            with self.assertRaisesRegex(ValueError, 'native failed'): self.publish()
        self.assert_previous()

    def test_partial_copy_failure_restores_metadata_and_version(self):
        original = updater.atomic_copy
        count = 0
        def failing(source, destination):
            nonlocal count
            count += 1
            if count == 2: raise OSError('injected disk failure')
            original(source, destination)
        with patch.object(updater, 'atomic_copy', side_effect=failing):
            with self.assertRaisesRegex(OSError, 'disk failure'): self.publish()
        self.assert_previous()

    def test_concurrent_edit_is_not_overwritten(self):
        (self.root / 'pin.json').write_text('developer edit')
        with self.assertRaisesRegex(ValueError, 'Source changed'): self.publish()
        self.assertEqual((self.root / 'pin.json').read_text(), 'developer edit')
        self.assertEqual(updater.sha(self.root / 'ProductVersion.props'), self.before['ProductVersion.props'])

    def test_completed_update_has_reusable_rollback(self):
        with patch.object(updater, 'local_identity'), patch.object(updater, 'version', return_value='3.2.0'):
            self.publish()
        self.assertFalse(updater.PENDING.exists())
        journal = updater.read(self.transaction / 'rollback.json')
        updater.rollback(journal)
        self.assert_previous()

    def test_rollback_preflights_all_concurrent_edits(self):
        with patch.object(updater, 'local_identity'), patch.object(updater, 'version', return_value='3.2.0'):
            self.publish()
        journal = updater.read(self.transaction / 'rollback.json')
        (self.root / 'ProductVersion.props').write_text('concurrent version edit')
        with self.assertRaisesRegex(ValueError, 'concurrent edit'): updater.rollback(journal)
        self.assertEqual((self.root / 'pin.json').read_text(), 'candidate-pin.json')

    def test_crash_recipe_survives_and_recovers(self):
        with patch.object(updater, 'local_identity'), patch.object(updater, 'version', return_value='3.2.0'):
            self.publish()
        updater.durable_journal(updater.read(self.transaction / 'rollback.json'))
        with self.assertRaisesRegex(ValueError, 'Incomplete publication'): updater.check()
        updater.rollback(updater.read(updater.PENDING)); self.assert_previous()

    def test_historical_model_rollback_does_not_restore_retired_voice_tracking(self):
        with patch.object(updater, 'local_identity'), patch.object(updater, 'version', return_value='3.2.0'):
            self.publish()
        journal = updater.read(self.transaction / 'rollback.json')
        for name in updater.RETIRED_VOICE_TRACKING:
            journal['files'].append(dict(path=name, before='old-catalog', after='retired-catalog'))
        updater.rollback(journal)
        self.assert_previous()
        for name in updater.RETIRED_VOICE_TRACKING:
            self.assertFalse((self.root / name).exists())


class SelectionTests(unittest.TestCase):
    def test_noop_and_configuration_block(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'reference').mkdir()
            (root / 'reference/model-config.yaml').write_text('flow_lm: {transformer: {num_layers: 6}}\n')
            updater.write(root / 'reference/pipeline-baseline.json', {'tokenizer': {'sha256': 'c'*64}})
            selected = dict(repo='kyutai/pocket-tts', filename='languages/english/model.safetensors', revision='b'*40,
                            digest='a'*64, git_revision='d'*40, config_url='official', config_bytes=b'config',
                            aliases=['stable'], config={'flow_lm': {'transformer': {'num_layers': 6}}}, pinned={})
            selected['config']['flow_lm']['lookup_table'] = {'tokenizer_path': 'hf://kyutai/tokenizer/tokenizer.json@'+'e'*40}
            (root / 'reference/model-config.yaml').write_text('flow_lm:\n  transformer: {num_layers: 6}\n  lookup_table:\n    tokenizer_path: hf://kyutai/tokenizer/tokenizer.json@'+'e'*40+'\n')
            contract = {'source_revision': 'b'*40, 'source_sha256': 'a'*64}
            paths_before = {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            tensor = SimpleNamespace(close=lambda: None)
            with patch.object(updater, 'ROOT', root), patch.object(updater, 'PENDING', root / 'pending.json'), \
                 patch.object(updater, 'local_identity', return_value=(contract, {'sha256': 'f'*64})), \
                 patch.object(updater, 'version', return_value='2.3.4'), \
                 patch.object(updater, 'prevent_downgrade') as downgrade, \
                 patch.object(updater.diagnostic, 'discover_official', return_value=selected), \
                 patch.object(updater.diagnostic, 'acquire', side_effect=[(root/'weights', {}), (root/'tokenizer', {'sha256': 'c'*64})]) as acquire, \
                 patch.object(updater.diagnostic, 'metadata', return_value={}), \
                 patch.object(updater.diagnostic, 'Tensors', return_value=tensor), \
                 patch.object(updater.diagnostic, 'inspect_olp', return_value=(tensor, {})), \
                 patch.object(updater.diagnostic, 'compare', return_value={'schema_equal': True, 'added': [], 'removed': [], 'schema_changed': []}):
                _, _, report = updater.check()
                self.assertEqual(report['status'], 'already-current')
                self.assertNotIn('proposed_engine_version', report)
                downgrade.assert_not_called()
                selected['config']['flow_lm']['transformer']['num_layers'] = 24
                acquire.side_effect = [(root/'weights', {})]
                with self.assertRaisesRegex(ValueError, 'Unsupported configuration changes'):
                    updater.check()
            self.assertEqual(paths_before, {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()})

    def test_divergent_revision_is_not_an_update(self):
        with patch('huggingface_hub.list_repo_commits', return_value=[SimpleNamespace(commit_id='new')]):
            with self.assertRaisesRegex(ValueError, 'downgrade/divergent'):
                updater.prevent_downgrade({'revision': 'new'}, {'source_revision': 'old'})

    def test_retired_weights_are_never_reselected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            updater.write(root / 'reference/model-history/retired.json', {'model': {'sha256': 'retired'}})
            with patch.object(updater, 'ROOT', root), patch('huggingface_hub.list_repo_commits', return_value=[SimpleNamespace(commit_id='new'), SimpleNamespace(commit_id='old')]):
                with self.assertRaisesRegex(ValueError, 'previously retired'):
                    updater.prevent_downgrade({'revision': 'new', 'digest': 'retired'}, {'source_revision': 'old'})


if __name__ == '__main__': unittest.main()
