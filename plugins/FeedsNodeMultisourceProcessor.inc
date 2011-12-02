<?php
// $Id: FeedsNodeMultisourceProcessor.inc,v 1.4 2010/10/08 13:55:19 andrewlevine Exp $

/**
 * @file
 * Class definition of FeedsNodeMultisourceProcessor.
 */

/**
 * Creates nodes from feed items.
 */
class FeedsNodeMultisourceProcessor extends FeedsNodeProcessor {

  /**
   * Implementation of FeedsProcessor::process().
   */
  public function process(FeedsImportBatch $batch, FeedsSource $source) {
    $parent_feed_info = $this->getParentFeedInfo($source);

    if (empty($parent_feed_info)) {
      drupal_set_message(t('Import could not be processed because no parent feed was specified to the additional node data processor.'), 'error');
      return FEEDS_BATCH_COMPLETE;
    }

    if (empty($this->config['content_type'])) {
      drupal_set_message(t('Import could not be processed because no destination content type was specified.'), 'error');
      return FEEDS_BATCH_COMPLETE;
    }

    // Keep track of processed items in this pass, set total number of items.
    $processed = 0;
    if (!$batch->getTotal(FEEDS_PROCESSING)) {
      $batch->setTotal(FEEDS_PROCESSING, $batch->getItemCount());
    }

    while ($item = $batch->shiftItem()) {

      // We are only processing updates because this is the additional node
      // data processor. Make sure we have a nid.
      if ($nid = $this->existingItemId($batch, $source)) {
        $node = $this->buildNode($nid, $parent_feed_info['nid']);
        $hash = $this->hash($item);

        // We are checking against our additional node feed's hashes here, not
        // the parent feed's hashes.
        if ($hash == $this->getAdditionalDataHash($nid, $source->feed_nid)) {
          continue;
        }

        // Map and save nodes. If errors occur don't stop but report them.
        try {
          $this->map($batch, $node);
          node_save($node);
          // Since we only updated feeds_node_item for the parent feed, let's
          // update it for our feed now.
          $feeds_node_item = new stdClass();
          $feeds_node_item->nid = $nid;
          $feeds_node_item->hash = $hash;
          $feeds_node_item->id = $this->id;
          $feeds_node_item->imported = FEEDS_REQUEST_TIME;
          $feeds_node_item->feed_nid = $source->feed_nid;
          $feeds_node_item->guid = $node->feeds_node_item->guid;
          $feeds_node_item->url = $node->feeds_node_item->url;

          // If we have already imported additional data to this node, update
          // our records. Or else, insert a new row marking additional data
          // imported.
          drupal_write_record('feeds_node_multisource_item', $feeds_node_item, array('nid', 'feed_nid'));
          if (!db_affected_rows()) {
            drupal_write_record('feeds_node_multisource_item', $feeds_node_item);
          }

          $batch->updated++;
        }
        catch (Exception $e) {
          drupal_set_message($e->getMessage(), 'warning');
          watchdog('feeds', $e->getMessage(), array(), WATCHDOG_WARNING);
        }
        $processed++;
      }
      else {
        // We are ignoring all nodes that haven't been created by the parent
        // feed yet.
        $batch->ignored++;
      }
      
      if ($processed >= variable_get('feeds_node_batch_size', FEEDS_NODE_BATCH_SIZE)) {
        $batch->setProgress(FEEDS_PROCESSING, $batch->updated + $batch->ignored);
        return;
      }
    }

    // Set messages.
    if ($batch->updated) {
      drupal_set_message(format_plural($batch->updated, 'Updated @number nodes.', 'Updated @number nodes.', array('@number' => $batch->updated,)));
    }
    else {
      drupal_set_message(t('There is no updated content.'));
    }
    
    if ($batch->ignored) {
      drupal_set_message(format_plural($batch->created, "Ignored @number items because the parent feed hasn't created it yet.", "Ignored @number items because the parent feed hasn't created them yet.", array('@number' => $batch->ignored,)));
    }

    $batch->setProgress(FEEDS_PROCESSING, FEEDS_BATCH_COMPLETE);
  }

  /**
   * Implementation of FeedsProcessor::clear().
   */
  public function clear(FeedsBatch $batch, FeedsSource $source) {
    // Get parent feed info from source
    $parent_feed_info = $this->getParentFeedInfo($source);
    
    if ($parent_feed_info) {
      $parent_feed_text = l(t('parent feed'), 'node/' . $parent_feed_info['nid']);
    }
    else {
      $parent_feed_text = t('parent feed');
    }
    drupal_set_message(t('Content cannot be deleted using an additional node data processor. If you want, you can delete the content from the !parent_feed_text.', array('!parent_feed_text' => $parent_feed_text)));
    $batch->setProgress(FEEDS_CLEARING, FEEDS_BATCH_COMPLETE);
  }

  /**
   * Implement expire().
   */
  public function expire($time = NULL) {
    return FEEDS_BATCH_COMPLETE;
  }

  /**
   * Override parent::configDefaults().
   * We want empty defaults to get rid of the FeedsNodeProcessor stuff.
   */
  public function configDefaults() {
    return array(
      'mappings' => array(),
      'content_type' => FALSE,
    );
  }

  /**
   * Return configuration form for this object. The keys of the configuration
   * form must match the keys of the array returned by configDefaults().
   *
   * @return
   *   FormAPI style form definition.
   */
  public function configForm(&$form_state) {
    $types = node_get_types('names');
    $form = array();
    $form['content_type'] = array(
      '#type' => 'select',
      '#title' => t('Desination content type'),
      '#description' => t('Select the type of node that will be updated by this feed.'),
      '#options' => $types,
      '#default_value' => $this->config['content_type'],
    );
    return $form;
  }

  /**
   * Add config form that allows us to pick a parent feed to save our additional
   * node data to.
   */
  public function sourceForm($source_config) {
    $form = array();
    $options = array();
    $result = db_query("SELECT id, feed_nid FROM {feeds_source} ORDER BY id ASC");
    while ($row = db_fetch_object($result)) {
      $options[$row->id . '|' . $row->feed_nid] = t("!id (NID: !nid)", array('!id' => $row->id, '!nid' => $row->feed_nid));
    }
    $form['parent_feed'] = array(
      '#type' => 'select',
      '#title' => t('Parent feed'),
      '#options' => $options,
      '#description' => t("Select the feed node you would like to attach this feed to. This feed will not create new nodes, it will only update nodes created by the parent feed."),
      '#default_value' => isset($source_config['parent_feed']) ? $source_config['parent_feed'] : '',
      '#required' => TRUE,
    );
    return $form;
  }

  /**
   * Validate the parent feed we added
   */
  public function sourceFormValidate(&$source_config) {
    $parent_feed_info = explode('|',  $source_config['parent_feed'], 2);
    $importer = feeds_importer($parent_feed_info[0]);
    $parent_config = $importer->processor->getConfig();

    if ($this->config['content_type'] != $parent_config['content_type']) {
      $dct_link = l(t('Destination content type'), 'admin/build/feeds/edit/' . $this->id . '/settings/' . get_class($this));
      $pfct_link = l(t('parent feed content type'), 'admin/build/feeds/edit/' . $importer->processor->id . '/settings/' . get_class($importer->processor));
      form_set_error('feeds][FeedsNodeMultisourceProcessor][parent_feed', t('!dct must be set to the same as the !pfct.', array('!dct' => $dct_link, '!pfct' => $pfct_link)));
    }
  }

  /**
  * Retrieve MD5 hash of the imported feed item from DB.
  * @return Empty string if no item is found, hash otherwise.
  */
  protected function getAdditionalDataHash($nid, $feed_nid) {
    $hash = db_result(db_query("SELECT hash FROM {feeds_node_multisource_item} WHERE nid = %d AND feed_nid = %d", $nid, $feed_nid));
    if ($hash) {
      // Return with the hash.
      return $hash;
    }
    return '';
  }


  /**
   * Get nid of an existing feed item node if available.
   * If the source_config didn't save correctly, return false so we can throw
   * an error to the user.
   */
  protected function existingItemId($batch, FeedsSource $source) {
    // Get parent feed info from source
    $parent_feed_info = $this->getParentFeedInfo($source);
    
    if (!$parent_feed_info) {
      // The source config wasn't set up correctly.
      return FALSE;
    }
     
    // Iterate through all unique targets and test whether they do already
    // exist in the database.
    foreach ($this->uniqueTargets($batch) as $target => $value) {
      switch ($target) {
        case 'url':
          $nid = db_result(db_query("SELECT nid FROM {feeds_node_item} WHERE feed_nid = %d AND id = '%s' AND url = '%s'", $parent_feed_info['nid'], $parent_feed_info['id'], $value));
          break;
        case 'guid':
          $nid = db_result(db_query("SELECT nid FROM {feeds_node_item} WHERE feed_nid = %d AND id = '%s' AND guid = '%s'", $parent_feed_info['nid'], $parent_feed_info['id'], $value));
          break;
      }
      if ($nid) {
        // Return with the first nid found.
        return $nid;
      }
    }
    return 0;
  }

  /**
   * Creates a new node object in memory and returns it.
   */
  protected function buildNode($nid, $feed_nid) {
    // Set config update_existing so parent buildNode() method will load our
    // current node.
    $this->config['update_existing'] = FEEDS_UPDATE_EXISTING;
    // Let our parent build the node to get most of the info ready.
    $node = parent::buildNode($nid, $feed_nid);
    // Make sure we log that the node was changed.
    $node->changed = FEEDS_REQUEST_TIME;
    // Log that the node was changed by FeedsNodeMultisourceProcessor.
    if (variable_get('feeds_node_multisource_update_log', TRUE)) {
      $node->log = 'Updated by FeedsNodeMultisourceProcessor';
    }
    return $node;
  }

  /**
   * Helper function that gets the parent feed information from the config data.
   */
  public function getParentFeedInfo(FeedsSource $source) {
    $source_config = $source->getConfigFor($this);
    if (empty($source_config['parent_feed'])) {
      // The config wasn't set up correctly.
      return FALSE;
    }
    // Identifying IDs from config form
    $parent_feed_info = explode('|',  $source_config['parent_feed'], 2);
    return array(
      'id' => $parent_feed_info[0],
      'nid' => $parent_feed_info[1],
    );
  }
}
