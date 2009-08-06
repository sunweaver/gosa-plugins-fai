<?php

class faiDiskEntry extends plugin
{
  /* attribute list for save action */
  var $ignore_account= TRUE;
  var $attributes   = array("DISKcn","DISKdescription");
  var $UsedAttrs            = array("cn","description","FAIpartitionType",
      "FAIpartitionNr","FAIfsType","FAImountPoint","FAIpartitionSize",
      "FAIfsTuneOptions", "FAIfsCreateOptions", "FAImountOptions",
      "FAIfsOptions","FAIpartitionFlags","FAIdiskOption");

  var $objectclasses= array();

  var $DISKcn              = "";
  var $DISKdescription     = "";
  var $DISKFAIdiskOptions  = "";
  
  var $partitions          = array();
  var $is_edit             = false;
  var $old_cn              = "";
  var $status              = "new";
  var $deletePartitions    = array();
  var $fstabkey            = "device";
  var $disklabel           = "msdos";
  var $FAIstate            = "";
  var $FAIdiskType         = "disk";
  
  var $lvmDevices          = array();

  function faiDiskEntry (&$config, $dn= NULL,$parent,$disk,$type)
  {
    plugin::plugin ($config, $dn);
  
    // Set default attributes 
    $this->parent = $parent;
    $this->FAIdiskType = $type;
    $this->status = "new";    

    // If disk is not empty, then we are going to edit 
    //  an existing disk, load disk info now. 
    if($disk){

      // Get devices used in volume base disks.
      if(isset($disk['FAIlvmDevice'])){
        $this->lvmDevices = $disk['FAIlvmDevice'];
      }

      // Get disk status
      $this->status = $disk['status'];

      // Load default attributes 
      $this->DISKcn          = $disk['cn'];
      $this->DISKdescription = $disk['description'];
      $this->is_edit         = true;
      $this->old_cn          = $disk['cn'];

      // Load partition info 
      foreach($disk['partitions'] as $name => $values){

        // Do not load removed partitions 
        if($values['status'] == "delete"){
          unset($disk['partitions'][$name]);
          $this->deletePartitions[]=$values;
        }else{

          /* If status is not new, set to edit mode. 
           * New means that this partition currently wasn't saved to ldap.
           */
          if($disk['partitions'][$name]['status']!="new"){
            $disk['partitions'][$name]['status']="edited";
          }
    
          // Load partition attributes  
          $disk['partitions'][$name]['old_cn']= $disk['partitions'][$name]['cn'];
          $disk['partitions'][$name]['FAIdiskType']= $this->FAIdiskType;
          foreach($this->UsedAttrs as $attr){
            if(!isset($values[$attr])){
              $disk['partitions'][$name][$attr]="";  
            }
          }
          if (preg_match('/^_/', $disk['partitions'][$name]['FAIfsType'])){
            $disk['partitions'][$name]['FAIfsType']= 
              preg_replace('/^_/', '', $disk['partitions'][$name]['FAIfsType']);
          }
        }
      }

      $this->partitions      = $disk['partitions'];

      /* Load FAIdiskOptions.
       * Some options are disk related and some are used for partitions.
       *  - fstabkey    -> disk
       *  - disklabel   -> disk
       *  - bootable    -> partition
       *  - preserve    -> partition
       *  - resize      -> partition
       */
      if (isset($disk['FAIdiskOption'])){
        foreach($disk['FAIdiskOption'] as $option) {

          // Get fstab key  
          if (preg_match("/fstabkey:(device|label|uuid)/", $option)){
            $this->fstabkey= preg_replace("/^.*fstabkey:(device|label|uuid).*$/", "$1", $option);
            continue;
          }

          // Get disk label
          if (preg_match("/disklabel:(msdos|gpt)/", $option)){
            $this->disklabel= preg_replace("/^.*disklabel:(msdos|gpt).*$/", "$1", $option);
            continue;
          }

          // Load bootable flag for partitions 
          if (preg_match("/^bootable:/", $option)){
            $bootable = split(",", trim(preg_replace("/^bootable:/","",$option),","));
            foreach($bootable as $bootflag){
              if(isset($this->partitions[$bootflag])){
                $this->partitions[$bootflag]['bootable'] = TRUE;  
              }
            }
            continue;
          }

          // Load resize flag for partitions 
          if (preg_match("/^resize:/", $option)){
            $resize = split(",", trim(preg_replace("/^resize:/","",$option),","));
            foreach($resize as $id){
              if(isset($this->partitions[$id])){
                $this->partitions[$id]['resize'] = TRUE;  
              }
            }
            continue;
          }

          // Load preserve_always flag for partitions 
          if (preg_match("/^preserve_always:/", $option)){
            $preserve = split(",", trim(preg_replace("/^preserve_always:/","",$option),","));
            foreach($preserve as $presflag){
              if(isset($this->partitions[$presflag])){
                $this->partitions[$presflag]['preserve'] = TRUE;  
                $this->partitions[$presflag]['preserveType'] = 'always';  
              }
            }
            continue;
          }

          // Load preserve_reinstall flag for partitions 
          if (preg_match("/^preserve_reinstall:/", $option)){
            $preserve = split(",", trim(preg_replace("/^preserve_reinstall:/","",$option),","));
            foreach($preserve as $presflag){
              if(isset($this->partitions[$bootflag])){
                $this->partitions[$presflag]['preserve'] = TRUE;  
                $this->partitions[$presflag]['preserveType'] = 'reinstall'; 
              } 
            }
            continue;
          }
        }
      } else {
        $this->fstabkey= "device";
      }
    }
  }


  function execute()
  {
    /* Call parent execute */
    plugin::execute();

    // Fill templating stuff
    $smarty     = get_smarty();
    $s_action   = "";
    $s_entry    = "";
    $display    = "";

    // Add partition to lvm compilation.
    if(isset($_POST['addLvmPartition']) && isset($_POST['lvmPartitionAdd'])){
      $name = get_post('lvmPartitionAdd');
      $this->lvmDevices[$name] = $name;
    }

    // Remove partition from lvm compilation.
    if(isset($_POST['delLvmPartition']) && isset($_POST['physicalPartition'])){
      $names = $_POST['physicalPartition'];
      foreach($names as $name){
        if(isset($this->lvmDevices[$name])){
          unset($this->lvmDevices[$name]);
        }
      }
    }

    /* Check all Posts if there is something usefull for us,
     * For example : Delete is posted as Delete_1 
     * The number specifies the index we want to delete
     */
    foreach($_POST as $name => $value){
      if((preg_match("/RemovePartition_/",$name)) && 
          $this->acl_is_removeable() && 
          !preg_match("/freeze/i",$this->FAIstate)){
        $tmp = split("_",$name);
        $this->removePartition($tmp[1]);
        break;
      }
      if(preg_match("/^EditPartition_/",$name)){
        $id = preg_replace("/^EditPartition_/","",$name);
        $id = preg_replace("/_.*$/","",$id);
        if(isset($this->partitions[$id])){
          $this->dialog = new faiPartition($this->config,$this->partitions[$id], $this,$this->FAIdiskType);
          break;
        }
      } 
    }

    /* Create a new partition for this disk.
     */
    if(isset($_POST['AddPartition']) && !preg_match("/freeze/i",$this->FAIstate)){
      $this->dialog = new faiPartition($this->config, array(), $this,$this->FAIdiskType);
    }

    /* Handle partition dialogs.
     */
    if($this->dialog instanceOf plugin && isset($_POST['PartitionCancel'])){
      $this->dialog = null;
    }
    if($this->dialog instanceOf plugin && isset($_POST['PartitionSave'])){
      $this->dialog->save_object();

      $new_partition = $this->dialog->save(); 
      $msgs = $this->dialog->check();
      $msgs = array_merge($this->check_disks($new_partition));

      if(!count($msgs)){
        $this->updatePartition($new_partition);
        $this->dialog = null;
      }else{
        msg_dialog::displayChecks($msgs);
      }
    }
    if($this->dialog instanceOf plugin){
      $this->dialog->save_object();
      return($this->dialog->execute());
    }

    // Assign checkbox related values.
    foreach($this->attributes as $attrs){
      $smarty->assign($attrs,$this->$attrs);
      if($this->$attrs){
        $smarty->assign($attrs."CHK"," ");
      }else{
        $smarty->assign($attrs."CHK"," disabled ");
      }
    }

    // Assign disk info to smarty.
    $smarty->assign("setup", $this->generateParts());
    $smarty->assign("sub_object_is_createable",$this->acl_is_createable());
    $smarty->assign("freeze",preg_match("/freeze/i",$this->FAIstate));
    $smarty->assign("fstabkeys", array("device" => _("Device"), "label" => _("Label"), "uuid" => _("UUID")));
    $smarty->assign("disklabels", array("msdos" => "MSDOS", "gpt" => "GPT"));
    $smarty->assign("fstabkey", $this->fstabkey);
    $smarty->assign("disklabel", $this->disklabel);
    $smarty->assign("FAIdiskType", $this->FAIdiskType);
    $smarty->assign("plist", $this->getPartitionList());
    $smarty->assign("physicalPartitionList", $this->getAvailablePartitions());

    foreach($this->attributes as $attr){
      $smarty->assign($attr,$this->$attr);
    }

    // Assign partitions
    $tmp = $this->plInfo();
    $sacl = "";
    foreach($tmp['plProvidedAcls'] as $name => $translated){
      $acl = $this->getacl($name, preg_match("/freeze/i",$this->FAIstate));
      $smarty->assign($name."ACL",$acl);
    }
   
    $display.= $smarty->fetch(get_template_path('faiDiskEntry.tpl', TRUE));
    return($display);
  }

  function getPartitionList()
  {
    $divlist = new divSelectBox("RaidList");

    /* Create a list of all available disks and partitions. 
     * This list will then be used to display detailed info.
     */
    $disks = $this->parent->disks;
    foreach($disks as $dname => $disk){

      // Skip currently edited disk 
      if($disk['cn'] == $this->old_cn) continue;
   
      // Add disk 
      $objs[$dname] = $disk;

      // Add disk partitions
      foreach($disk['partitions'] as $id => $part){
        $part['parentDisk'] = $disk;
        $objs[$part['cn']] = $part;
      }
    }

    // Attach current disk setup to the details list.
    $data = $this->save();
    $objs[$data['cn']] = $data;
    foreach($data['partitions'] as $part){
      $part['parentDisk'] = $data;
      $objs[$part['cn']] = $part;
    }

    // Walk through physical partition combinations and build up 
    //  user friendly list with partition details.
    $list = array();
    foreach($this->lvmDevices as $device){

      // We've a html select box here, add spaces for better readability
      $str = $device;
      $str = preg_replace("/ /","&nbsp;",str_pad($str,20," "));

      // Add disk/partition details.
      if(isset($objs[$device])){
        if(isset($objs[$device]['FAIpartitionSize'])){
          if($objs[$device]['parentDisk']['FAIdiskType'] == "raid"){
            $str .= _("Disks").":&nbsp;";
            $str .= $objs[$device]['FAIpartitionSize'];
          }else{
            $str .= _("Size").":&nbsp;";
            $str .= $objs[$device]['FAIpartitionSize'];
          } 
        }
      }
      $list[$device] = $str;
    }
    return($list);
  }

 
  /* Returns a list of available partitions that are useable in this 
   *  lvm disk setup.
   */ 
  function getAvailablePartitions()
  {
    $array = array();
    foreach($this->parent->disks as $disk){

      // We can't add ourselves to the available partitions
      if($disk['cn'] == $this->old_cn) continue;

      // Add partitions to the list.
      foreach($disk['partitions'] as $key => $part){
        $name = $part['cn'];
        if(!isset($this->lvmDevices[$name])){
          $array[$name] = $name;
        }
      }
    }
    return($array);

  }


  /* Remove the selected partition and shift the following partitions 
   *  to fill gap.
   * Additionally update the partition numbers correspondingly.
   */
  function removePartition($id)
  {
    $start = false;
    foreach($this->partitions as $key => $part){
      if($id == $key){
        $start = true;
      }
      if($start){
        if($this->partitions[$key]['status'] == "edited"){
          $this->deletePartitions[$key]= $this->partitions[$key];
          $this->deletePartitions[$key]['FAIpartitionNr']=$key;
          unset($this->partitions[$key]);
        }else{
          unset($this->partitions[$key]);
        }
        if(isset($this->partitions[($key+1)])){
          if(isset($this->deletePartitions[$key])){
            unset($this->deletePartitions[$key]);
          }
          $this->partitions[$key] = $this->partitions[($key+1)];
          $this->partitions[$key]['FAIpartitionNr'] = $key;
          $this->partitions[$key]['status'] = "new";
        }
      }
    }
    $tmp= array();
    foreach($this->partitions as $part){
      $tmp[count($tmp)+1]=$part;
    }
    $this->partitions = $tmp;
  }


  /* Add or update a partition 
   */
  function updatePartition($part)
  {
    if(!isset($part['FAIpartitionNr']) || $part['FAIpartitionNr'] == "undefined"){
      $part['FAIpartitionNr'] = count($this->partitions) + 1;
    }

    /* Update the disk cn -       
     * Do NOT touch the partition 'cn' in case of lvm devices. 
     */
    if($this->FAIdiskType == "disk"){
      $part['cn'] = $this->DISKcn.$part['FAIpartitionNr'];
    }

    /* Check if we've to update lvm compilations.
     */ 
    if($this->FAIdiskType == "lvm"){
      if(isset($this->partitions[$part['FAIpartitionNr']])){
        $old_cn = $this->partitions[$part['FAIpartitionNr']]['cn'];
        $new_cn = $part['cn'];
        if(isset($this->lvmDevices[$old_cn])){
          unset($this->lvmDevices[$old_cn]);
          $this->lvmDevices[$new_cn] = $new_cn;
        }
      }
    }

    $this->partitions[$part['FAIpartitionNr']] = $part;
  }


  function generateParts()
  {

    $divlist = new divSelectBox("DiskEntries"); 
    foreach($this->partitions as $key => $part){
      $cn =array(
          "string" => $part['cn'],
          "attach" => "style='width:20px;'");
      $number =array(
          "string" => $part['FAIpartitionNr'],
          "attach" => "style='width:20px;'");
      $size   =array(
          "string" => $part['FAIpartitionSize'],
          "attach" => "style='width:100px;'");
      $fstype =array(
          "string" => $part['FAIfsType'],
          "attach" => "style='width:60px;'");
      $type   =array(
          "string" => $part['FAIpartitionType'],
          "attach" => "style='width:80px;'");
      $opt    =array(
          "string" => $part['FAImountOptions'],
          "attach" => "style='width:80px;'");
      $fsopt  =array(
          "string" => $part['FAIfsOptions'],
          "attach" => "style='width:80px;'");
      $flags  =array(
          "string" => $part['FAIpartitionFlags'],
          "attach" => "style='width:80px;'");
      $mntp   =array("string" => $part['FAImountPoint']);

   
      $action =array(
          "string" => "<input type='image' src='images/lists/edit.png' name='EditPartition_".$key."'>".
                      "<input type='image' src='images/lists/trash.png' name='RemovePartition_".$key."'>",
          "attach" => "style='width:40px; border-right: 0px;'");

      if($this->FAIdiskType == "lvm"){ 
        $fields = array($cn,$mntp,$size,$fstype, $opt,$fsopt,$flags,$action);
      }else{
        $fields = array($number,$type,$mntp,$size,$fstype, $opt,$fsopt,$flags,$action);
      }
      $divlist->AddEntry($fields);
    }
    return($divlist->DrawList());    
  }


  function save()
  {
    $tmp = array();
    $tmp['cn']          = $this->DISKcn;

    /* Attach partitions */
    foreach($this->partitions as $key=>$val) {
      $this->partitions[$key]['FAIpartitionNr']=$key;

      if($this->FAIdiskType == "disk"){
        $this->partitions[$key]['cn'] = $this->DISKcn.$key;
      }elseif($this->FAIdiskType == "lvm"){
        $this->partitions[$key]['FAIpartitionType'] = 'lvm';
      }
    }

    /* Attach deleted */
    foreach($this->deletePartitions as $key=>$val) {
      $this->partitions[$key."-delete"]=$val;
      $this->partitions[$key."-delete"]['status']="delete";
    }

    $tmp['description'] = $this->DISKdescription;
    $tmp['partitions']  = $this->partitions;
    $tmp['status']      = $this->status;
    $tmp['FAIdiskType'] = $this->FAIdiskType;

    $tmp['FAIlvmDevice'] = array();
    foreach($this->lvmDevices as $dev){
      $tmp['FAIlvmDevice'][] = $dev;
    } 

    /* Assemble flags */
    $tmp['FAIdiskOption'] = array("fstabkey:".$this->fstabkey, "disklabel:".$this->disklabel);

    /* If hdd name has changed, tell partitionTable to rename it */
    if(($this->is_edit)&&($this->old_cn != $this->DISKcn)){
      $tmp['rename']['from']  = $this->old_cn;
      $tmp['rename']['to']    = $this->DISKcn;
    }

    // Build up disk options 
    $bootable = "";
    $resize = "";
    $preserve_always = "";
    $preserve_reinstall = "";

    foreach($tmp['partitions'] as $id => $part){
      if(isset($part['bootable']) && $part['bootable']){
        $bootable .= $id.",";
      }
      if(isset($part['resize']) && $part['resize']){
        $resize .= $id.",";
      }
      if(isset($part['preserve']) && $part['preserve']){
        if($part['preserveType'] == "always"){
          $preserve_always .= $id.",";
        }else{
          $preserve_reinstall .= $id.",";
        }
      }
      $tmp['partitions'][$id]['status'] = $part['status'];

      // Unset non valid attributes 
      foreach(array("bootable","encrypted","preserve","preserveType","resize","FAIdiskType") as $attr){
        if(isset($tmp['partitions'][$id][$attr])){
          unset($tmp['partitions'][$id][$attr]);
        }
      }
    }    

    if(!empty($bootable)){
      $tmp['FAIdiskOption'][] = "bootable:".trim($bootable,",");
    }
    if(!empty($resize)){
      $tmp['FAIdiskOption'][] = "resize:".trim($resize,",");
    }
    if(!empty($preserve_always)){
      $tmp['FAIdiskOption'][] = "preserve_always:".trim($preserve_always,",");
    }
    if(!empty($preserve_reinstall)){
      $tmp['FAIdiskOption'][] = "preserve_reinstall:".trim($preserve_reinstall,",");
    }

    $tmp['status'] = $this->status;
    return($tmp);
  }


  /* Save data to object */
  function save_object()
  {
    if((isset($_POST['TableEntryFrameSubmitted'])) && !preg_match("/freeze/", $this->FAIstate) ){
      plugin::save_object();

      // Save posted disk label and fstab key
      if (isset($_POST['disklabel']) && preg_match("/^(msdos|gpt)$/", $_POST['disklabel'])){
        $this->disklabel= $_POST['disklabel'];
      }
      if (isset($_POST['fstabkey']) && preg_match("/^(device|label|uuid)$/", $_POST['fstabkey'])){
        $this->fstabkey= $_POST['fstabkey'];
      }
    }
  }


  /* Check supplied data */
  function check()
  {
    /* Call common method to give check the hook */
    $message= plugin::check();
  
    /* Check for an empty disk name */
    $d = trim($this->DISKcn);
    if($d == "" ){
      $message[] = msgPool::required(_("Name"));
    }
    if(preg_match("/[^a-z0-9_\-]/i",$d)){
      $message[] = msgPool::invalid(_("Name"),$d,"/[a-z0-9_\-]/i");
    }
    return ($message);
  }


  /* Checks the disk combinations.
   * 
   */  
  function check_disks($disk_to_add = array())
  {
    $msgs = array();

    /* Check 'disk' combinations. 
     *  - There can be four primary partitions.
     *  - If there is at least one 'logical' partition, then there can be only 
     *     three 'primary' partitions.
     */    
    if($this->FAIdiskType == "disk"){
     
      $types = array('logical' => array(), 'primary' => array());
      $types[$disk_to_add['FAIpartitionType']][$disk_to_add['FAIpartitionNr']] = 1;
      foreach($this->partitions as $key => $part){
        $types[$part['FAIpartitionType']][$part['FAIpartitionNr']] = 1;
      }

      // There can only be four primary partitions per disk - without any logical partition.
      if(count($types['logical']) == 0){
        if(count($types['primary']) > 4){
          $msgs[] = _("You have more than four primary partition table entries in your configuration, please check your configuration twice.");
        }
      }else{
        if(count($types['primary']) > 3){
          $msgs[] = _("You cannot have more than three primary partition while using logical partitions, please check your configuration twice.");
        }
      }
    }

    return($msgs);
  }  


  /* Return plugin informations for acl handling */
  static function plInfo()
  {
    return (array(
          "plShortName" => _("Partition table entry"),
          "plDescription" => _("FAI partition table entry"),
          "plSelfModify"  => FALSE,
          "plDepends"     => array(),
          "plPriority"    => 27,
          "plSection"     => array("administration"),
          "plCategory"    => array("fai"),
          "plProvidedAcls" => array(
            "DISKcn"           => _("Name"),
            "DISKdescription"  => _("Description"),
            "DISKFAIdiskOption"  => _("Disk options"),
            "FAIpartitionType"  => _("Partition type"),
            "FAIpartitionNr"    => _("Partition no."),
            "FAIfsType"         => _("File system type"),
            "FAImountPoint"     => _("Mount point"),
            "FAIpartitionSize"  => _("Partition size"),
            "FAImountOptions"   => _("Mount options"),
            "FAIfsOptions"      => _("File system options"),
            "FAIpartitionFlags" => _("Partition flags"))
          ));
  }

 
}

// vim:tabstop=2:expandtab:shiftwidth=2:filetype=php:syntax:ruler:
?>