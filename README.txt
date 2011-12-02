$Id: README.txt,v 1.1 2010/07/08 21:39:49 andrewlevine Exp $

Feeds Node Multisource allows you to take data from multiple different feeds and
import them into the same nodes. The module provides a Feeds processor that
allows you to import data into existing nodes that have been created by a
"parent" feed. A child-parent relationship between feeds is created where the
feed with the "Node processor" is the parent and the feeds with the "Node
Multisource Processor" are the children. A child feed will never create nodes,
it will simply update the nodes that have been created by the parent feed.

To use this module:
1. Install Feeds module and this module according to
   http://drupal.org/node/70151
2. Create a "parent" feed that uses the regular "Node processor". This feed will
   be the one actually creating nodes
3. Make sure "Update existing item nodes" is selected in the processor settings
   under "Update existing items"
4. Create a child feed that uses the "Node Multisource Processor"
5. Under the processor settings, make sure the "Destination content type" is the
   same as the "Content type" you set in the parent feed processor settings
6. Set up the mappings on the child feed just as you would for a regular "Node
   processor". Make sure you have set up a unique target that matches a parent
   feed unique target (This is the only way Feeds will know which node to save
   to)
7. Run an import on your parent feed and then run an import on your child feed.
   If the two feeds have content with unique targets that match, the child feed
   should have updated the nodes created by the parent feed with data.